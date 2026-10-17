// Editor schermate: stato + azioni + tela. La logica sta in editor/actions.js, i moduli ui-*.js sono sottili.
import { createStore } from './editor/store.js';
import { createActions, initialState } from './editor/actions.js';
import { createAssets } from './editor/assets.js';
import { mountCanvas } from './editor/ui-canvas.js';
import { mountTopbar } from './editor/ui-topbar.js';
import { mountDrawers } from './editor/ui-drawers.js';
import { mountAdd, mountLayers } from './editor/ui-layers.js';
import { mountProps } from './editor/ui-props.js';
import { mountWarnings } from './editor/ui-warnings.js';
import { mountSend } from './editor/ui-send.js';
import { listThemes, getTheme, createTheme, checkTheme, putTheme, importTheme, deleteTheme, sendTheme, getStatus, watchStatus } from './api.js';

const store = createStore(initialState());
const A = createActions(store, { listThemes, getTheme, createTheme, checkTheme, putTheme, importTheme, deleteTheme, sendTheme, getStatus });
mountTopbar(store, A);
mountDrawers(store, A);
mountAdd(store, A);
mountLayers(store, A);
mountProps(store, A);
mountWarnings(store, A);
mountSend(store, A, watchStatus);
mountCanvas(store, A, createAssets());
const q = new URLSearchParams(location.search);
// solo per le prove nel browser (smoke-editor.mjs)
if (q.get('test') === '1') window.__editor = { store, A };
A.init({ want: q.get('theme') });
