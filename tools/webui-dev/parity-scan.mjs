// Scansione di parità dell'editor (F14, rafforzata dopo la revisione del Task 9): nessun pixel dell'anteprima può
// nascere fuori dal renderer condiviso. Regole sui file dell'editor (editor/ ricorsiva, editor.js, ui-*.js):
//  - niente nomi che disegnano da sé (drawText, T(…), drawAvatar…), niente import() dinamici, require, worker;
//  - chiamate del canvas (getContext, fillRect, drawImage…) solo in editor/view.js, e lì fillRect/strokeRect solo
//    sul contesto `c`, mai scritture sulla bitmap del renderer;
//  - ogni file ha il suo elenco di import ammessi (specificatore normalizzato → nomi); niente alias, `* as`,
//    import predefiniti, import solo per effetto, riesportazioni;
//  - un nome importato da un modulo fuori dall'editor (ui.js, api.js, theme/…) deve essere definito lì,
//    non riesportato da un altro modulo.
import path from 'node:path';

// elenco di oggi: aggiungere qui (con giudizio) quando un task nuovo importa qualcosa
export const ALLOW = {
  'editor.js': {
    'editor/store.js': ['createStore'], 'editor/actions.js': ['createActions', 'initialState'], 'editor/assets.js': ['createAssets'],
    'editor/ui-canvas.js': ['mountCanvas'], 'editor/ui-topbar.js': ['mountTopbar'], 'editor/ui-drawers.js': ['mountDrawers'],
    'editor/ui-layers.js': ['mountAdd', 'mountLayers'], 'editor/ui-props.js': ['mountProps'], 'editor/ui-warnings.js': ['mountWarnings'],
    'editor/ui-send.js': ['mountSend'],
    'api.js': ['listThemes', 'getTheme', 'createTheme', 'checkTheme', 'putTheme', 'importTheme', 'deleteTheme', 'sendTheme', 'getStatus', 'watchStatus'],
  },
  'editor/actions.js': {
    'editor/history.js': ['createHistory', 'commit', 'endGesture', 'undo', 'redo'],
    'editor/ops.js': ['ensureIds', 'findEl', 'moveEls', 'setBox', 'removeEls', 'duplicateEls', 'copyEls', 'pasteEls', 'groupEls', 'ungroup', 'refusal', 'SCALED', 'addEl', 'updateEl', 'reorder', 'elementDefaults', 'updateVariant',
      'newTheme', 'renameTheme', 'addVariant', 'removeVariant', 'moveVariant', 'fromStock'],
    'editor/catalog.js': ['fieldsFor', 'FONT_CHOICES', 'templateProblem', 'condOk', 'condCount', 'SCREENS'],
    'editor/layers.js': ['cleanName'],
    'editor/select.js': ['pruneIds', 'layout', 'marquee'], 'editor/geometry.js': ['snapMove', 'snapResize', 'align', 'distribute'],
    'editor/warnings.js': ['boardVariant', 'sendBlock'], 'editor/testdata.js': ['testModel'], 'theme/lower.js': ['SCREEN_KEYS'], 'theme/stock.js': ['STOCK'],
    'model.js': ['deviceName'],
  },
  'editor/assets.js': { 'avatar.js': ['loadAvatar'], 'api.js': ['loadAnchors', 'loadAccessory'] },
  'editor/catalog.js': {
    'theme/lower.js': ['SCREEN_KEYS'], 'theme/fonts.js': ['FONTS'], 'editor/ops.js': ['elementDefaults'], 'editor/warnings.js': ['elementLabel'],
    'theme/glyphs.js': ['VALUE_GLYPHS'], 'model.js': ['STATES', 'POSE_LABEL'],
  },
  'editor/geometry.js': { 'editor/select.js': ['selectionBox'] },
  'editor/gesture.js': { 'editor/select.js': ['hitTest', 'handleAt'] },
  'editor/history.js': {},
  'editor/keys.js': {},
  'editor/ops.js': { 'theme/lower.js': ['SCREEN_KEYS'] },
  'editor/select.js': { 'editor/ops.js': ['findEl', 'LOCKED_SCREENS'] },
  'editor/store.js': {},
  'editor/testdata.js': { 'theme/values.js': ['makeModel'] },
  'editor/ui-canvas.js': {
    'editor/view.js': ['renderPreview', 'paintBitmap', 'paintOverlay', 'fitZoom', 'scaleFor'], 'editor/select.js': ['layout', 'handles', 'handleNames', 'marquee', 'resizeBox'],
    'editor/ops.js': ['findEl'], 'editor/warnings.js': ['liveWarnings'], 'editor/testdata.js': ['testModel', 'BASE'], 'editor/catalog.js': ['SCREENS'],
    'editor/gesture.js': ['createGesture'], 'editor/keys.js': ['canvasKey'],
  },
  'editor/layers.js': {
    'theme/lower.js': ['lowerTheme'], 'theme/values.js': ['condsTrue', 'urgentIdx', 'utf8Cut'], 'editor/warnings.js': ['elementLabel'],
    'editor/testdata.js': ['testModel'], 'editor/ops.js': ['LOCKED_SCREENS'],
  },
  'editor/ui-drawers.js': {},
  'editor/ui-layers.js': { 'ui.js': ['h'], 'editor/catalog.js': ['TYPES'], 'editor/layers.js': ['layerRows', 'dropTarget', 'stepTarget'] },
  'editor/ui-props.js': {
    'ui.js': ['h'], 'editor/catalog.js': ['fieldsFor', 'FONT_CHOICES', 'WHEN', 'REFS', 'CONDS', 'SCREENS', 'condKey', 'condDefault', 'valueChoices', 'templateProblem'],
    'editor/ops.js': ['findEl', 'SCALED'], 'editor/select.js': ['layout', 'selectionBox'], 'editor/warnings.js': ['elementLabel'], 'theme/glyphs.js': ['glyphWarnings'],
  },
  'editor/ui-warnings.js': {
    'ui.js': ['h'], 'editor/warnings.js': ['liveWarnings', 'elementLabel', 'sizeInfo', 'warnCount', 'variantBanner', 'warningPlace'], 'editor/select.js': ['layout'], 'editor/testdata.js': ['testModel', 'BASE'], 'editor/ops.js': ['findEl'],
    'editor/catalog.js': ['SCREENS'],
  },
  'editor/ui-send.js': { 'ui.js': ['h'], 'editor/actions.js': ['sendLine'] },
  'editor/ui-topbar.js': {
    'ui.js': ['h'], 'editor/catalog.js': ['SCREENS', 'WHEN'], 'editor/history.js': ['canUndo', 'canRedo'], 'editor/actions.js': ['saveText', 'themeChanges'],
  },
  'editor/view.js': { 'theme/render.js': ['renderScreen'], 'theme/surface.js': ['Bitmap'], 'editor/warnings.js': ['previewTheme'] },
  'editor/warnings.js': {
    'theme/lower.js': ['lowerTheme', 'parseTemplate', 'SCREEN_KEYS'], 'theme/render.js': ['renderScreen'], 'theme/values.js': ['whenTrue', 'refIndex', 'expand', 'makeModel'],
    'theme/text.js': ['textWidthMilli', 'wrapLines', 'hasFont'], 'theme/glyphs.js': ['glyphWarnings'], 'theme/surface.js': ['Null'],
  },
};

const DRAWS = /\b(drawAvatar|drawText|paintRows|drawAccessory|renderWith|heroGoccia|importScripts|require)\b|\bT\s*\(|\bimport\s*\(|\bnew\s+(Shared)?Worker\b|\beval\s*\(|\bnew\s+Function\b|\bFunction\s*\(/;
const CANVAS = /\b(getContext|fillRect|strokeRect|fillText|strokeText|drawImage|putImageData|createImageData|getImageData|OffscreenCanvas|transferToImageBitmap|createImageBitmap)\b|createElement\s*\(\s*['"`]canvas/;
const BITMAP_WRITE = /\bbitmap\s*\.\s*(fillRect|px\s*\[[^\]]*\]\s*(=(?!=)|\+=|-=|\|=|&=|\^=))|\.px\s*\.\s*(fill|set|copyWithin)\s*\(|\.px\s*\[[^\]]*\]\s*(=(?!=)|\+=|-=|\|=|&=|\^=)/;

export const isEditorFile = (rel) => rel === 'editor.js' || rel.startsWith('editor/') || /^ui-[^/]*\.js$/.test(rel);

// specificatore → percorso relativo ad assets/js (senza query, senza «./»), o null se esce dalla cartella
export function resolveSpec(fromRel, spec) {
  const clean = spec.replace(/[?#].*$/, '');
  if (!clean.startsWith('./') && !clean.startsWith('../')) return null;
  const r = path.posix.normalize(path.posix.join(path.posix.dirname(fromRel), clean));
  return r.startsWith('../') || r === '..' ? null : r;
}

// import ed export…from di un sorgente: [{ kind: 'import'|'reexport', clause, spec, quote }]
function statements(src) {
  const out = [];
  for (const m of src.matchAll(/(?:^|;)[ \t]*import\s*(?:([^;'"`]*?)\s*\bfrom\s*)?(['"`])([^'"`]*)\2/gm)) out.push({ kind: 'import', clause: m[1] ?? null, quote: m[2], spec: m[3] });
  for (const m of src.matchAll(/(?:^|;)[ \t]*export\s*(\*(?:\s*as\s+[\w$]+)?|\{[^}]*\})\s*from\s*(['"`])([^'"`]*)\2/gm)) out.push({ kind: 'reexport', clause: m[1], quote: m[2], spec: m[3] });
  return out;
}

const definesLocally = (src, name) => new RegExp(`\\bexport\\s+(?:async\\s+)?(?:function\\s*\\*?|class|const|let|var)\\s+${name.replace(/\$/g, '\\$')}\\b`).test(src);

// files: { rel: src } dei file dell'editor; read(rel) → sorgente di un modulo fuori dall'editor (o null)
export function scan(files, read) {
  const bad = [];
  const add = (rel, msg) => bad.push(rel + ': ' + msg);
  for (const [rel, src] of Object.entries(files)) {
    const d = src.match(DRAWS);
    if (d) add(rel, 'disegna da sé o carica codice al volo (' + d[0] + ')');
    if (rel !== 'editor/view.js') {
      const c = src.match(CANVAS);
      if (c) add(rel, 'usa il canvas fuori da view.js (' + c[0] + ')');
    } else {
      const all = (src.match(/\.\s*(fillRect|strokeRect)\s*\(/g) || []).length, onCtx = (src.match(/\bc\.(fillRect|strokeRect)\s*\(/g) || []).length;
      if (all !== onCtx) add(rel, 'fillRect/strokeRect fuori dal contesto del canvas');
    }
    // la bitmap del renderer non si tocca in nessun file
    const w = src.match(BITMAP_WRITE);
    if (w) add(rel, 'scrive sulla bitmap del renderer (' + w[0] + ')');
    const allow = ALLOW[rel];
    if (!allow) { add(rel, 'file senza elenco di import ammessi (parity-scan.mjs)'); continue; }
    const sts = statements(src);
    // ogni «from '…'» deve essere un import o un export riconosciuto (niente import nascosti a metà riga)
    const froms = (src.match(/\bfrom\s*['"`]/g) || []).length;
    if (froms !== sts.filter((x) => x.kind === 'reexport' || x.clause !== null).length) add(rel, 'import che la scansione non sa leggere');
    for (const st of sts) {
      if (st.kind === 'reexport') { add(rel, 'riesporta da ' + st.spec); continue; }
      if (st.quote === '`') { add(rel, 'specificatore con template: ' + st.spec); continue; }
      const target = resolveSpec(rel, st.spec);
      if (!target) { add(rel, 'import fuori da assets/js o non relativo: ' + st.spec); continue; }
      if (st.clause === null) { add(rel, 'import solo per effetto: ' + st.spec); continue; }
      const m = /^\{([^}]*)\}$/.exec(st.clause.trim());
      if (!m) { add(rel, 'import predefinito o «* as» da ' + st.spec + ' (' + st.clause.trim() + ')'); continue; }
      const names = m[1].split(',').map((x) => x.trim()).filter(Boolean);
      if (names.some((n) => /\s+as\s+/.test(n))) { add(rel, 'import con alias da ' + st.spec); continue; }
      const ok = allow[target];
      if (!ok) { add(rel, 'import non ammesso: ' + target); continue; }
      for (const n of names) if (!ok.includes(n)) add(rel, 'nome non ammesso: ' + n + ' da ' + target);
      if (!isEditorFile(target)) {
        const dep = read(target);
        if (dep === null) { add(rel, 'modulo mancante: ' + target); continue; }
        for (const n of names) if (!definesLocally(dep, n)) add(rel, n + ' non è definito in ' + target + ' (riesportato?)');
      }
    }
  }
  return bad;
}
