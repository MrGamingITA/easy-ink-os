// Azioni dell'editor: tutta la logica fra lo stato (store.js), le operazioni pure e il servizio (api).
// Niente window/document: si prova in Node con un api e dei timer finti. I moduli ui-*.js chiamano solo queste.
import { createHistory, commit, endGesture, undo, redo } from './history.js';
import { ensureIds, findEl, moveEls, setBox, removeEls, duplicateEls, copyEls, pasteEls, groupEls, ungroup, refusal, SCALED, addEl, updateEl, reorder, elementDefaults, updateVariant, newTheme, renameTheme, addVariant, removeVariant, moveVariant, fromStock } from './ops.js';
import { cleanName } from './layers.js';
import { pruneIds, layout, marquee } from './select.js';
import { snapMove, snapResize, align, distribute } from './geometry.js';
import { fieldsFor, FONT_CHOICES, templateProblem, condOk, condCount, SCREENS } from './catalog.js';
import { boardVariant, sendBlock } from './warnings.js';
import { deviceName } from '../model.js';
import { testModel } from './testdata.js';
import { SCREEN_KEYS } from '../theme/lower.js';
import { STOCK } from '../theme/stock.js';

const CHECK_MS = 300, SAVE_MS = 1500, FRAME_MS = 500;
const FOLLOW_MS = 1000, FOLLOW_MAX = 120;   // invio: un GET /api/status al secondo, al più 2 minuti di controlli
const RETRY_MS = [5000, 15000, 60000];   // riprova automatica dopo un errore (poi ogni 60 s)
const LOCAL_MAX = 5;                      // versioni messe da parte da «Ricarica», per tema
const STUCK = new Set(['conflict', 'gone']);   // si sbloccano solo con una scelta di chi modifica
const TEST_SESSIONS = [0, 1, 6];
const TEST_FLAGS = ['lowBatt', 'night', 'perm', 'pcOff'];
const NAME_MAX = 24;   // nome del tema (F22, parse.go)
const COPY = ' copia';
const ONLY_SCREEN = 'L\'unica variante dell\'unica schermata non si toglie: un tema deve avere almeno una schermata.';
const NO_STOCK = 'Arriva con le schermate di serie come dati';

const problemsText = (n) => (n === 1 ? '1 problema' : `${n} problemi`);
const utf8 = (s) => new TextEncoder().encode(s).length;
const cutBytes = (s, n) => { let a = Array.from(String(s)); while (a.length && utf8(a.join('')) > n) a = a.slice(0, -1); return a.join('').trim(); };

// testo dello stato del salvataggio nella barra
export function saveText(save) {
  switch (save?.state) {
    case 'pending': return 'Modifiche in sospeso…';
    case 'saving': return 'Salvo…';
    case 'invalid': return save.msg || 'Non salvato';
    case 'error': return (save.msg || 'Non salvato') + (save.retry ? ' · Riprovo…' : '');
    case 'conflict': return 'Cambiato altrove';
    case 'gone': return 'Eliminato altrove';
    default: return 'Salvato';
  }
}

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const sameJSON = (a, b) => JSON.stringify(a) === JSON.stringify(b);
// ---------- «Invia alla scheda» (Task 14) ----------

// schede a cui si può mandare un tema: solo 250×122 (la classe S dei temi); senza themes.supported c'è ma non si sceglie
export function boards(status) {
  const ds = Array.isArray(status?.displays) ? status.displays : [];
  return ds.filter((d) => isObj(d) && typeof d.board === 'string' && d.w === 250 && d.h === 122).map((d) => {
    const ok = d.themes?.supported === true;
    return { board: d.board, label: `${d.board} · ${deviceName(d.device)}`, ok, why: ok ? '' : 'la scheda non ha ancora detto quali temi ha' };
  });
}

const SEND_TEXT = { salvo: 'Salvo prima di inviare…', attesa: 'In attesa della scheda…', offerta: 'Offerto alla scheda…', scaricato: 'Scaricato, lo attivo…', uso: 'Attivazione…', fatto: 'Tema attivo sulla scheda' };
// send = { board, theme, stage, error, job, note }; «rifiutato» = non inviato, col motivo
export function sendText(send) {
  if (!send) return '';
  const t = send.stage === 'errore' ? 'Non riuscito: ' + (send.error || 'errore sconosciuto')
    : send.stage === 'rifiutato' ? send.error : SEND_TEXT[send.stage] ?? send.stage;
  return send.note ? `${t} · ${send.note}` : t;
}
const SEND_FINAL = new Set(['fatto', 'errore', 'rifiutato']);
// riga d'invio sotto il pulsante (M1): col nome del tema se non è quello aperto; «modificato dopo l'invio» se il
// tema aperto, attivo sulla scheda, non è più quello inviato
export function sendLine(s) {
  const send = s?.send, t = sendText(send);
  if (!t) return '';
  if (send.theme !== s.themeId) return `«${send.name || send.theme}»: ${t}`;
  const now = s.hist?.present;
  if (send.stage === 'fatto' && send.sent && now && now !== send.sent && !sameJSON(now, send.sent)) return `${t} · modificato dopo l'invio`;
  return t;
}

// cosa cambia passando dal primo tema al secondo (per chi sceglie fra la versione del servizio e la propria)
export function themeChanges(from, to) {
  const out = [];
  if ((from?.name ?? '') !== (to?.name ?? '')) out.push(`il nome («${from?.name ?? ''}» → «${to?.name ?? ''}»)`);
  const a = from?.screens ?? {}, b = to?.screens ?? {};
  const keys = [...new Set([...Object.keys(a), ...Object.keys(b)])];
  const order = (k) => { const i = SCREENS.findIndex((x) => x.key === k); return i < 0 ? 99 : i; };
  keys.sort((x, y) => order(x) - order(y) || (x < y ? -1 : 1));
  for (const k of keys) if (!sameJSON(a[k], b[k])) out.push(SCREENS.find((x) => x.key === k)?.label ?? k);
  const rest = (t) => { const { name, screens, ...r } = t ?? {}; return r; };
  if (!sameJSON(rest(from), rest(to))) out.push('altre impostazioni del tema');
  return out;
}

export function initialState() {
  return {
    themes: [], themeId: null, hist: null, check: null,
    loc: { screen: 'home', variant: 0 }, sel: [], scope: null,
    zoom: 4, grid: true, snap: { grid: 1, others: true },
    test: { sessions: 1, lowBatt: false, night: false, perm: false, pcOff: false },
    playing: false, frame: 0, clock: 0,
    save: { state: 'saved', msg: '', base: 0, local: 0, retry: false }, send: null,
    clip: null, touchMulti: false,
    unsaved: [],  // altri temi con modifiche non salvate o messe da parte: [{ id, name, state, msg, local }]
    drawer: null, error: '',
    notice: '',   // perché l'ultima operazione non ha cambiato niente (ops.refusal, addEl…)
  };
}

const LOCKED = 'L\'elemento è bloccato.';
const ROW = 'Un elemento che usa i valori della riga ({riga.…}) può stare solo dentro una lista.';
const GRIDS = [1, 2, 4, 8];
// condizioni più numerose sotto un gruppo: per ogni discendente le sue più quelle dei gruppi intermedi
// (i nascosti non si compilano; le righe di una lista non ereditano, la lista sì)
function deepestConds(children) {
  let best = 0;
  for (const c of Array.isArray(children) ? children : []) {
    if (!isObj(c) || c.hidden === true) continue;
    const own = condCount(c.visibleIf);
    best = Math.max(best, own + (c.type === 'group' ? deepestConds(c.children) : 0));
  }
  return best;
}
const variantCount = (theme, screen) => {
  const vs = theme?.screens?.[screen]?.variants;
  return Array.isArray(vs) ? vs.length : 0;
};

// risultato di un'operazione: tema, oppure { theme, id | ids, reason } (F4: id null = niente fatto)
function normalize(r, theme) {
  if (isObj(r) && isObj(r.theme) && !('screens' in r) && !('format' in r)) {
    return { theme: r.theme, ids: Array.isArray(r.ids) ? r.ids : (typeof r.id === 'string' ? [r.id] : null), reason: typeof r.reason === 'string' ? r.reason : null };
  }
  return { theme: isObj(r) ? r : theme, ids: null, reason: null };
}

export function createActions(store, api, { now = Date.now, setTimeout: later = globalThis.setTimeout, clearTimeout: cancel = globalThis.clearTimeout, stock = STOCK } = {}) {
  // per tema, per tutta la sessione: cronologia, base del servizio, ultimo tema salvato, stato del salvataggio,
  // modifiche messe da parte da «Ricarica» (pila, al massimo LOCAL_MAX), riprova automatica dopo un errore.
  // La base vale solo per `saved`: mai una base nuova sopra un contenuto vecchio (revisione Task 9, I2).
  const recs = new Map();   // id → { hist, base, saved, save: {state, msg}, local: [hist], inflight, tries, retryT }
  let openSeq = 0;
  const get = () => store.get();
  const rec = () => recs.get(get().themeId) ?? null;
  const dirty = (r) => r.hist.present !== r.saved;
  const freshRec = (theme, base) => { const t = ensureIds(theme); return { hist: createHistory(t), base, saved: t, save: { state: 'saved', msg: '' }, local: [], inflight: null, tries: 0, retryT: null }; };
  const nameOf = (id, r) => r?.hist?.present?.name || id;
  const saveView = (r) => ({ state: r.save.state, msg: r.save.msg, base: r.base, local: r.local.length, retry: r.retryT !== null });
  // stato del tema aperto e degli altri temi con lavoro non salvato (I2)
  function publish() {
    const id = get().themeId, r = rec();
    const unsaved = [...recs].filter(([k, x]) => k !== id && (dirty(x) || x.local.length))
      .map(([k, x]) => ({ id: k, name: nameOf(k, x), state: x.save.state, msg: x.save.msg, local: x.local.length }));
    store.set({ unsaved, ...(r ? { save: saveView(r) } : {}) });
  }
  // una versione messa da parte; oltre LOCAL_MAX si scarta la più vecchia (e lo si dice)
  const pushLocal = (r, hist) => { r.local.push(hist); if (r.local.length <= LOCAL_MAX) return ''; r.local.shift(); return ` Se ne tengono ${LOCAL_MAX}: la più vecchia è stata scartata.`; };
  const cancelRetry = (r) => { if (r.retryT !== null) cancel(r.retryT); r.retryT = null; };
  // ciclo di salvataggio del tema aperto: controllo dopo 300 ms, PUT dopo 1 500 ms senza modifiche (Task 13)
  let checkT = null, saveT = null, playT = null, checkFor = null, checking = null;
  const stopTimers = () => { if (checkT !== null) cancel(checkT); if (saveT !== null) cancel(saveT); checkT = saveT = null; };
  function schedule() {
    stopTimers();
    const id = get().themeId, r = rec();
    if (!r) return;
    checkT = later(() => { checkT = null; if (get().themeId === id) refreshCheck(); }, CHECK_MS);
    if (dirty(r) && !STUCK.has(r.save.state)) saveT = later(() => { saveT = null; if (get().themeId === id) A.autosave(); }, SAVE_MS);
  }
  // errore di rete o del servizio: si riprova da soli dopo 5 s, 15 s, poi ogni 60 s
  function scheduleRetry(id, r) {
    cancelRetry(r);
    const ms = RETRY_MS[Math.min(r.tries, RETRY_MS.length - 1)];
    r.tries++;
    r.retryT = later(() => {
      r.retryT = null;
      if (recs.get(id) !== r || r.save.state !== 'error') return;
      if (get().themeId === id) {
        if (gest !== null) { scheduleRetry(id, r); r.tries--; return; }   // trascinamento in corso: più tardi
        closeEdit(r);
      }
      A.saveNow({ id });
    }, ms);
  }
  // gesto in corso (trascinamento, maniglia): tema di partenza, a cui si applica lo scarto cumulativo
  let gest = null;
  const gestureBase = (merge) => {
    const present = get().hist.present;
    if (merge === null) return { base: present };
    if (!gest || gest.key !== merge) gest = { key: merge, base: present };
    return gest;
  };
  // un passo aperto che non è un trascinamento (un campo di Proprietà col fuoco, una freccia tenuta) si chiude:
  // così la cronologia e il salvato coincidono (revisione Task 13, I1)
  function closeEdit(r) {
    const s = get();
    if (gest !== null || !s.hist || s.hist.open === null || recs.get(s.themeId) !== r) return;
    r.hist = endGesture(s.hist);
    store.set({ hist: r.hist });
  }

  // variante d'apertura: quella che la scheda sceglierebbe coi dati di prova, altrimenti la prima (F19)
  const boardOr0 = (theme, screen, test) => Math.max(0, boardVariant(theme, screen, testModel(test)));

  function setHist(hist, extra = {}) {
    const s = get(), r = rec();
    if (r) {
      r.hist = hist;
      // un conflitto (o un tema eliminato altrove) resta finché non si sceglie; una modifica nuova riparte da capo
      if (!STUCK.has(r.save.state)) { r.save = { state: dirty(r) ? 'pending' : 'saved', msg: '' }; cancelRetry(r); r.tries = 0; }
    }
    const scope = s.scope && !pruneIds(hist.present, s.loc, [s.scope]).length ? null : s.scope;
    store.set({ hist, scope, ...(r ? { save: saveView(r) } : {}), ...extra });
    if (r) schedule();
  }

  // controllo del servizio (avvisi, byte per schermata); scartato se nel frattempo il tema è cambiato
  function refreshCheck() {
    const t = get().hist?.present;
    if (!t || !api.checkTheme) return Promise.resolve();
    const p = api.checkTheme(t).then((c) => { if (get().hist?.present === t) { checkFor = t; store.set({ check: c }); } }, () => {});
    checking = { t, p };
    return p;
  }

  // ---------- invio alla scheda: un invio alla volta, poi il controllo dell'operazione ----------
  let sending = false, fol = null;   // fol = { board, theme, job, polls, t, paused }
  const setSend = (send) => store.set({ send });
  const screenLabel = (k) => SCREENS.find((x) => x.key === k)?.label ?? k;
  const stopFollow = () => { if (fol?.t != null) cancel(fol.t); fol = null; };
  async function doSend(board, id, r) {
    stopFollow();
    const name = nameOf(id, r);
    const no = (msg) => { setSend({ board, theme: id, name, stage: 'rifiutato', error: msg, job: null, note: '' }); return false; };
    closeEdit(r);
    const block = sendBlock(r.hist.present);
    if (block) return no(`Non inviato: la scheda mostrerebbe la schermata di serie al posto di «${screenLabel(block.screen)}» (${block.reason}). Correggi prima di inviare.`);
    setSend({ board, theme: id, name, stage: 'salvo', error: '', job: null, note: '' });
    // modifiche arrivate durante il salvataggio: si salva ancora (al più 3 giri)
    for (let k = 0; k < 3 && (dirty(r) || r.inflight) && !STUCK.has(r.save.state); k++) {
      if (r.inflight) { await r.inflight.catch(() => {}); continue; }
      stopTimers();
      if (!(await A.autosave())) break;
    }
    if (recs.get(id) !== r) return no('Il tema è cambiato durante il salvataggio: non inviato.');
    switch (r.save.state) {
      case 'conflict': return no('Il tema è cambiato altrove: scegli «Ricarica» o «Sovrascrivi» prima di inviare.');
      case 'gone': return no('Il tema è stato eliminato altrove: «Salva come nuovo» prima di inviare.');
      case 'invalid': return no('Correggi i problemi prima di inviare');
      case 'error': return no(`Il tema non è salvato (${r.save.msg || 'errore'}): riprova prima di inviare.`);
      default: break;
    }
    if (dirty(r)) { schedule(); return no('Modifiche in corso: il tema non è ancora salvato, riprova fra un attimo.'); }
    let job;
    const sent = r.hist.present;   // salvato: è quello che la scheda riceve
    try { job = await api.sendTheme(board, id); }
    catch (e) { setSend({ board, theme: id, name, stage: 'errore', error: e?.message || String(e), job: null, note: '' }); return false; }
    const stage = typeof job?.stage === 'string' ? job.stage : 'attesa';
    setSend({ board, theme: id, name: nameOf(id, r), sent, stage, error: job?.error || '', job: job?.id ?? null, note: '' });
    if (!SEND_FINAL.has(stage)) { fol = { board, theme: id, job: job?.id ?? null, polls: 0, t: null, paused: false }; followStep(FOLLOW_MS); }
    return true;
  }
  // un GET /api/status dopo ms; si ferma alla fase finale, a un'operazione sostituita o al tetto (mai un ciclo stretto)
  function followStep(ms) {
    const f = fol;
    if (!f || f.paused) return;
    f.t = later(async () => {
      f.t = null;
      if (fol !== f || f.paused) return;
      f.polls++;
      let status = null;
      try { status = api.getStatus ? await api.getStatus() : null; } catch { /* servizio giù: si riprova al prossimo giro */ }
      if (fol !== f) return;
      const d = Array.isArray(status?.displays) ? status.displays.find((x) => x?.board === f.board) : null;
      const j = d?.themes?.job;
      const cur = get().send;
      if (isObj(j) && (f.job === null || j.id === f.job)) {
        const next = { ...cur, stage: String(j.stage || cur.stage), error: j.error || '' };
        if (next.stage !== cur.stage || next.error !== cur.error) setSend(next);
        if (SEND_FINAL.has(next.stage)) { fol = null; return; }
      } else if (isObj(j) && j.id !== f.job) {
        setSend({ ...cur, stage: 'errore', error: 'un altro invio a questa scheda l\'ha sostituito' });
        fol = null;
        return;
      }
      if (f.polls >= FOLLOW_MAX) { setSend({ ...get().send, note: 'nessuna notizia da 2 minuti: la scheda lo riceve quando si ricollega' }); fol = null; return; }
      followStep(FOLLOW_MS);
    }, ms);
  }

  const A = {
    // elenco dei temi e apertura di ?theme=<id> o del primo (F19)
    async init({ want = null } = {}) {
      let list;
      try { list = await api.listThemes(); }
      catch (e) { store.set({ error: 'Non riesco a leggere i temi: ' + (e?.message || e) }); return false; }
      store.set({ themes: list, error: '' });
      const id = list.some((t) => t.id === want) ? want : list[0]?.id;
      return id ? A.openTheme(id) : false;
    },

    // salva subito un tema (di norma quello aperto). La base va SEMPRE: un 409 diventa conflitto, mai una sovrascrittura.
    // `over`: base del file vivo letta da «Sovrascrivi» (solo dal conflitto, dopo la conferma); vale solo per questo PUT
    // e lo manda con fresh: versione nuova, quella dell'altro dispositivo resta in cronologia
    async saveNow({ over = 0, id = get().themeId } = {}) {
      const r = recs.get(id);
      if (!r) return true;
      // un solo salvataggio per tema: chi arriva dopo aspetta, poi salva con la base nuova se c'è ancora qualcosa
      while (r.inflight) await r.inflight.catch(() => {});
      if (recs.get(id) !== r) return false;   // eliminato o ricaricato nel frattempo
      if (!api.putTheme || (!dirty(r) && !over)) return true;
      if (STUCK.has(r.save.state) && !over) return false;
      cancelRetry(r);
      // senza base l'autosave non parte: sarebbe una sovrascrittura alla cieca
      const base = over || r.base;
      if (!(base > 0)) { r.save = { state: 'error', msg: 'Non salvato: manca la versione letta dal servizio' }; publish(); return false; }
      const sent = r.hist.present;
      r.save = { state: 'saving', msg: '' }; publish();
      const mine = (async () => {
        try {
          const info = await api.putTheme(id, sent, over ? { base, fresh: true } : { base });
          r.base = Number(info?.updated) || base;
          r.saved = sent;
          r.tries = 0;
          r.save = { state: dirty(r) ? 'pending' : 'saved', msg: '' };
          return true;
        } catch (e) {
          if (e?.name === 'ConflictError') r.save = { state: 'conflict', msg: 'Cambiato altrove' };
          else if (e?.name === 'InvalidError') {
            // i problemi del 400 vanno nel controllo, così l'elenco degli avvisi li mostra (mai «N problemi» senza righe)
            const ps = Array.isArray(e.problems) && e.problems.length ? e.problems : [{ path: '', msg: e.message || 'tema non valido' }];
            r.save = { state: 'invalid', msg: 'Non salvato: ' + problemsText(ps.length) };
            if (get().themeId === id && get().hist?.present === sent) store.set({ check: { ...(get().check ?? {}), problems: ps } });
          }
          else if (e?.name === 'NotFoundError') r.save = { state: 'gone', msg: 'Eliminato altrove' };
          else { r.save = { state: 'error', msg: 'Non salvato: ' + (e?.message || e) }; scheduleRetry(id, r); }
          return false;
        }
      })();
      r.inflight = mine;
      try { return await mine; } finally { if (r.inflight === mine) r.inflight = null; publish(); }
    },

    async openTheme(id) {
      const tok = ++openSeq;
      const cur = get().themeId;
      let left = '';
      if (cur && cur !== id) {
        // niente modifiche in sospeso lasciate indietro; se il salvataggio non riesce lo si dice (I2)
        const r0 = recs.get(cur);
        if (r0) closeEdit(r0);
        if (!(await A.saveNow()) && recs.get(cur) && dirty(recs.get(cur))) {
          const r1 = recs.get(cur);
          left = `Il tema «${nameOf(cur, r1)}» non è salvato (${saveText(saveView(r1))}): resta fra i temi non salvati.`;
        }
      }
      if (tok !== openSeq) return false;
      stopTimers();
      // un PUT ancora in volo per questo tema: prima finisce, poi si legge (niente base vecchia, M4)
      const prev = recs.get(id);
      while (prev?.inflight) await prev.inflight.catch(() => {});
      if (tok !== openSeq) return false;
      let res;
      try { res = await api.getTheme(id); }
      catch (e) { if (tok === openSeq) store.set({ error: `Non riesco ad aprire il tema «${id}»: ` + (e?.message || e), notice: left }); return false; }
      if (tok !== openSeq) return false;
      const base = res.base || 0;
      // ensureIds in silenzio: solo una modifica vera rende il tema «in sospeso» (F19)
      let r = recs.get(id), notice = '';
      if (!r) r = freshRec(res.theme, base);
      else if (r.base !== base) {
        // cambiato altrove da quando l'abbiamo letto: vale la versione del servizio
        const old = r;
        cancelRetry(old);
        r = freshRec(res.theme, base);
        r.local = old.local;
        if (dirty(old)) {
          notice = 'Il tema è stato cambiato altrove: aperta la versione del servizio. Le modifiche locali non salvate sono state messe da parte.' + pushLocal(r, old.hist);
        } else notice = 'Il tema è stato cambiato altrove: aperta la versione aggiornata.';
      }
      recs.set(id, r);
      const s = get();
      const screen = SCREEN_KEYS.includes(s.loc.screen) ? s.loc.screen : 'home';
      store.set({
        themeId: id, hist: r.hist, check: null, sel: [], scope: null, error: '', notice: [left, notice].filter(Boolean).join(' '),
        loc: { screen, variant: boardOr0(r.hist.present, screen, s.test) },
      });
      publish();
      checkFor = null;
      refreshCheck();
      if (dirty(r) && !STUCK.has(r.save.state)) schedule();
      return true;
    },

    // modifiche messe da parte (la più recente, come cronologia), o null; tutta la pila con localStack
    localChanges(id = get().themeId) { return recs.get(id)?.local.at(-1) ?? null; },
    localStack(id = get().themeId) { return [...(recs.get(id)?.local ?? [])]; },

    // ---------- salvataggio automatico e conflitti (Task 13) ----------

    // allo scadere dei 1 500 ms: mai durante un trascinamento, mai un tema che il controllo dice non valido.
    // Un campo di testo aperto non blocca: il suo passo si chiude e si salva il testo attuale (I1).
    async autosave() {
      const id = get().themeId, r = rec();
      if (!r || !dirty(r) || STUCK.has(r.save.state)) return false;
      if (gest !== null) return false;   // riparte da endGesture
      closeEdit(r);
      const t = r.hist.present;
      // il controllo dei 300 ms ancora in volo per questo stesso tema: si aspetta quello
      if (checkFor !== t && checking?.t === t) await checking.p;
      let c = checkFor === t ? get().check : null;
      if (!c && api.checkTheme) {
        try { c = await api.checkTheme(t); } catch { c = null; }   // controllo giù: decide il PUT (400 = invalid)
        if (c && get().hist?.present === t) { checkFor = t; store.set({ check: c }); }
      }
      if (recs.get(id) !== r || r.hist.present !== t || STUCK.has(r.save.state)) return false;   // cambiato: ci pensa il prossimo timer
      const n = Array.isArray(c?.problems) ? c.problems.length : 0;
      if (n) { r.save = { state: 'invalid', msg: 'Non salvato: ' + problemsText(n) }; publish(); return false; }
      return A.saveNow();
    },

    // la pagina si nasconde (app in sottofondo, chiusura): si chiude il passo aperto e si salva subito ogni tema,
    // senza il controllo (M2: lo rifà il servizio). true = niente lasciato in sospeso
    async saveOnHide() {
      const r = rec();
      if (r) closeEdit(r);
      stopTimers();
      const ids = [...recs].filter(([, x]) => dirty(x) && !STUCK.has(x.save.state)).map(([k]) => k);
      const done = await Promise.all(ids.map((id) => A.saveNow({ id })));
      return done.every(Boolean);
    },

    // «Riprova» dopo un errore (subito, senza aspettare)
    async retrySave() {
      const r = rec();
      if (!r || STUCK.has(r.save.state)) return false;
      stopTimers();
      cancelRetry(r); r.tries = 0;
      if (r.save.state === 'error' || r.save.state === 'invalid') { r.save = { state: dirty(r) ? 'pending' : 'saved', msg: '' }; publish(); }
      checkFor = null;
      return A.autosave();
    },
    // «Riprova» per un altro tema rimasto non salvato (I2); conflitti ed eliminati si risolvono aprendolo
    async retryTheme(id) {
      const r = recs.get(id);
      if (!r || STUCK.has(r.save.state)) return false;
      if (id === get().themeId) return A.retrySave();
      cancelRetry(r); r.tries = 0;
      return A.saveNow({ id });
    },

    // c'è qualcosa, in qualunque tema, che chiudendo la pagina andrebbe perso (anche le modifiche messe da parte)
    unsaved() { return [...recs.values()].some((r) => dirty(r) || r.local.length > 0 || r.inflight); },

    // «Ricarica»: vale la versione del servizio; le mie modifiche vanno in cima alla pila delle messe da parte
    async reload() {
      const id = get().themeId, r = rec();
      if (!r) return false;
      stopTimers();
      cancelRetry(r);
      while (r.inflight) await r.inflight.catch(() => {});
      let res;
      try { res = await api.getTheme(id); }
      catch (e) { store.set({ notice: 'Non riesco a ricaricare il tema: ' + (e?.message || e) }); return false; }
      if (get().themeId !== id || recs.get(id) !== r) return false;
      const next = freshRec(res.theme, res.base || 0);
      next.local = r.local;
      const dropped = dirty(r) ? pushLocal(next, r.hist) : '';
      recs.set(id, next);
      const t = next.hist.present, s = get(), n = variantCount(t, s.loc.screen);
      const loc = s.loc.variant < n || n === 0 ? s.loc : { ...s.loc, variant: n - 1 };
      store.set({ hist: next.hist, loc, sel: [], scope: null, check: null, notice: dropped.trim() });
      publish();
      checkFor = null;
      refreshCheck();
      return true;
    },

    // la versione messa da parte più recente torna come un passo sopra quella aperta (si annulla), poi si salva
    restoreLocal() {
      const r = rec(), local = r?.local.at(-1);
      if (!local) return false;
      r.local.pop();
      A.commit(() => local.present);
      publish();
      return true;
    },
    // scarta la versione messa da parte più recente (la pagina chiede prima)
    dropLocal() {
      const r = rec();
      if (!r?.local.length) return false;
      r.local.pop();
      publish();
      return true;
    },

    // per «Sovrascrivi»: la versione viva del servizio, la sua base e cosa cambierebbe sovrascrivendola
    async conflictDiff() {
      const id = get().themeId, r = rec();
      if (!r) return null;
      let res;
      try { res = await api.getTheme(id); }
      catch (e) { store.set({ notice: 'Non riesco a leggere la versione del servizio: ' + (e?.message || e) }); return null; }
      const theirs = ensureIds(res.theme);
      return { base: res.base || 0, theirs, changes: themeChanges(theirs, r.hist.present) };
    },

    // «Sovrascrivi», dopo la conferma: PUT con la base appena letta. Se nel frattempo è cambiato ancora → di nuovo conflitto.
    async overwrite(base) {
      const r = rec();
      if (!r || r.save.state !== 'conflict' || !(Number.isInteger(base) && base > 0)) return false;
      stopTimers();
      return A.saveNow({ over: base });
    },

    // tema eliminato altrove (404 al salvataggio, M3): il contenuto in memoria diventa un tema nuovo (import)
    async saveAsNew() {
      const id = get().themeId, r = rec();
      // un solo import anche con due tocchi di fila
      if (!r || r.save.state !== 'gone' || r.busy) return null;
      r.busy = true;
      let info;
      try { info = await api.importTheme(JSON.stringify(r.hist.present)); }
      catch (e) { r.busy = false; store.set({ notice: 'Non salvato come nuovo: ' + (e?.message || e) }); return null; }
      recs.delete(id);
      await A.refreshThemes();
      if (!(await A.openTheme(info.id))) { r.busy = false; recs.set(id, r); return null; }
      // le versioni messe da parte passano al tema nuovo, mai perse (N1)
      const n = recs.get(info.id);
      if (n && r.local.length) { n.local = [...n.local, ...r.local].slice(-LOCAL_MAX); publish(); }
      return info.id;
    },

    // ---------- «Invia alla scheda» (Task 14) ----------

    boards,

    // Prima salva subito (niente attesa dei 1 500 ms): si invia solo la versione salvata, mai una vecchia.
    // Rifiuta con un conflitto aperto, problemi, salvataggio non riuscito o una schermata che la scheda scarterebbe.
    // Poi POST e controllo dell'operazione in GET /api/status. → true = inviato (le fasi arrivano dopo)
    async send(board) {
      const id = get().themeId, r = rec();
      if (!r || !api.sendTheme || sending) return false;
      sending = true;
      try { return await doSend(board, id, r); } finally { sending = false; }
    },
    // pagina nascosta: niente controlli; di nuovo visibile: un controllo subito (se non si è già al tetto)
    pauseFollow() { if (fol) { fol.paused = true; if (fol.t !== null) cancel(fol.t); fol.t = null; } },
    resumeFollow() { if (fol?.paused) { fol.paused = false; followStep(0); } },

    // ---------- temi (Task 13) ----------

    async refreshThemes() {
      try { store.set({ themes: await api.listThemes() }); return true; }
      catch (e) { store.set({ notice: 'Non riesco a leggere i temi: ' + (e?.message || e) }); return false; }
    },

    // nuovo tema col nome dato (id dal nome, libero); → id o null
    async newTheme(name) {
      const t = newTheme(name, get().themes.map((x) => x.id));
      let info;
      try { info = await api.createTheme(t); }
      catch (e) { store.set({ notice: 'Non creato: ' + (e?.message || e) }); return null; }
      const id = info?.id || t.id;
      await A.refreshThemes();
      return (await A.openTheme(id)) ? id : null;
    },

    // copia del tema aperto (anche le modifiche non ancora salvate) con «<nome> copia»; l'id lo sceglie il servizio
    async duplicateTheme() {
      const s = get();
      if (!s.hist) return null;
      const t = s.hist.present;
      const name = cutBytes(t.name || t.id || 'Tema', NAME_MAX - utf8(COPY)) + COPY;
      return A._import(JSON.stringify({ ...t, name }), 'Non duplicato: ');
    },

    // solo il nome: l'id resta (la scheda riconosce i temi per id)
    renameTheme(name) {
      if (!get().hist) return false;
      const ok = A.commit((t) => renameTheme(t, name));
      const want = String(name ?? '').replace(/\s+/g, ' ').trim(), got = get().hist.present.name;
      if (ok && want && got !== want) store.set({ notice: `Nome accorciato a 24 byte: «${got}».` });
      return ok;
    },

    // dopo la conferma della pagina; poi si apre il primo tema che resta
    async deleteTheme() {
      const id = get().themeId, r = rec();
      if (!id) return false;
      stopTimers();
      if (r) { cancelRetry(r); while (r.inflight) await r.inflight.catch(() => {}); }
      try { await api.deleteTheme(id); }
      catch (e) { store.set({ notice: 'Non eliminato: ' + (e?.message || e) }); if (r && dirty(r)) schedule(); return false; }
      recs.delete(id);
      openSeq++;
      store.set({ themeId: null, hist: null, check: null, sel: [], scope: null, save: { state: 'saved', msg: '', base: 0, local: 0, retry: false } });
      publish();
      checkFor = null;
      await A.refreshThemes();
      const first = get().themes[0]?.id;
      if (first) await A.openTheme(first);
      return true;
    },

    exportTheme() {
      const s = get();
      if (!s.hist) return null;
      return { filename: (s.themeId || 'tema') + '.theme.json', text: JSON.stringify(s.hist.present, null, 2) + '\n' };
    },

    // un file .theme.json: l'id lo sceglie il servizio; → id o null (motivo in notice)
    importTheme(text) { return A._import(text, 'Non importato: '); },
    async _import(text, prefix) {
      let info;
      try { info = await api.importTheme(String(text ?? '')); }
      catch (e) {
        const ps = Array.isArray(e?.problems) ? e.problems : [];
        const why = e?.name === 'InvalidError' && ps.length
          ? problemsText(ps.length) + ' (' + ps.slice(0, 2).map((p) => (p.path ? p.path + ': ' : '') + p.msg).join('; ') + ')'
          : (e?.message || String(e));
        store.set({ notice: prefix + why });
        return null;
      }
      await A.refreshThemes();
      return (await A.openTheme(info.id)) ? info.id : null;
    },

    // ---------- varianti e schermata di serie (Task 13) ----------

    addVariant(when = 'always') {
      const s = get();
      if (!s.hist) return false;
      let index = null;
      const ok = A.commit((t) => {
        const r = addVariant(t, s.loc.screen, when);
        index = r.index;
        return r.index === null ? { theme: t, id: null, reason: r.reason } : r.theme;
      });
      if (ok) store.set({ loc: { screen: s.loc.screen, variant: index }, sel: [], scope: null });
      return ok;
    },
    removeVariant() {
      const s = get();
      if (!s.hist) return false;
      const ok = A.commit((t) => {
        const out = removeVariant(t, s.loc.screen, s.loc.variant);
        return out === t ? { theme: t, id: null, reason: variantCount(t, s.loc.screen) ? ONLY_SCREEN : null } : out;
      });
      if (ok) {
        const n = variantCount(get().hist.present, s.loc.screen);
        store.set({ loc: { screen: s.loc.screen, variant: Math.max(0, Math.min(s.loc.variant, n - 1)) }, sel: [], scope: null });
      }
      return ok;
    },
    // dir −1 = su, +1 = giù; la variante aperta segue
    moveVariant(dir) {
      const s = get();
      if (!s.hist || (dir !== -1 && dir !== 1)) return false;
      const to = s.loc.variant + dir;
      const ok = A.commit((t) => moveVariant(t, s.loc.screen, s.loc.variant, to));
      if (ok) store.set({ loc: { screen: s.loc.screen, variant: to } });
      return ok;
    },
    hasStock(screen = get().loc.screen) { return Object.hasOwn(stock, screen) && !!stock[screen]; },
    // la conferma («Sostituire la schermata…») la chiede la pagina
    fromStock() {
      const s = get();
      if (!s.hist) return false;
      if (!A.hasStock(s.loc.screen)) { store.set({ notice: NO_STOCK }); return false; }
      const ok = A.commit((t) => fromStock(t, s.loc.screen, stock[s.loc.screen]));
      if (ok) store.set({ loc: { screen: s.loc.screen, variant: boardOr0(get().hist.present, s.loc.screen, s.test) }, sel: [], scope: null });
      return ok;
    },

    // ---------- dati di prova e animazione (Task 13) ----------

    // sessions 0/1/6 e i quattro interruttori; la variante aperta non cambia (la scritta sopra la tela lo dice)
    setTest(patch) {
      if (!isObj(patch)) return false;
      const cur = get().test, next = { ...cur };
      if (TEST_SESSIONS.includes(patch.sessions)) next.sessions = patch.sessions;
      for (const k of TEST_FLAGS) if (typeof patch[k] === 'boolean') next[k] = patch[k];
      if (sameJSON(next, cur)) return false;
      store.set({ test: next });
      return true;
    },
    // 2 fps: un solo timer alterna il fotogramma e manda avanti l'orologio; fermo = fotogramma 0 e ora di BASE
    togglePlay() {
      if (playT !== null) { cancel(playT); playT = null; }
      if (get().playing) { store.set({ playing: false, frame: 0, clock: 0 }); return false; }
      const tick = () => {
        playT = later(() => {
          const s = get();
          if (!s.playing) { playT = null; return; }
          store.set({ frame: s.frame ? 0 : 1, clock: s.clock + FRAME_MS });
          tick();
        }, FRAME_MS);
      };
      store.set({ playing: true, frame: 0, clock: 0 });
      tick();
      return true;
    },

    // Permessi e le altre bloccate non si aprono mai
    setLoc(loc) {
      const s = get();
      if (!isObj(loc) || !SCREEN_KEYS.includes(loc.screen)) return false;
      const t = s.hist?.present, n = variantCount(t, loc.screen);
      let variant;
      if (loc.variant === undefined || loc.variant === null) variant = boardOr0(t, loc.screen, s.test);
      else variant = Number.isInteger(loc.variant) && loc.variant >= 0 && loc.variant < n ? loc.variant : 0;
      store.set({ loc: { screen: loc.screen, variant }, sel: [], scope: null, notice: '' });
      return true;
    },

    // fn(theme) → theme | { theme, id | ids, reason }; merge = chiave del gesto (un trascinamento = un passo)
    commit(fn, merge = null) {
      const s = get();
      if (!s.hist) return false;
      const theme = s.hist.present;
      let r;
      try { r = fn(theme); }
      catch (e) {
        console.error('editor: operazione non riuscita', e);
        store.set({ notice: 'Errore interno: operazione non riuscita' });
        return false;
      }
      const res = normalize(r, theme);
      if (res.theme === theme) { if (res.reason) store.set({ notice: res.reason }); return false; }
      const hist = commit(s.hist, res.theme, merge);
      if (hist === s.hist) return false;
      setHist(hist, { sel: pruneIds(res.theme, s.loc, res.ids ?? s.sel), notice: '' });
      return true;
    },

    endGesture() {
      gest = null;
      const s = get();
      if (s.hist && s.hist.open !== null) setHist(endGesture(s.hist));
    },

    // ---------- tela (Task 10) ----------

    // trascinamento: scarto cumulativo dall'inizio del gesto (F13), applicato alla base del gesto,
    // agganciato (solo gli assi mossi) e arrotondato all'intero. Restituisce le guide da disegnare.
    moveSel(dx, dy, merge = null, { snap = true } = {}) {
      const s = get();
      if (!s.hist || !s.sel.length || !Number.isFinite(dx) || !Number.isFinite(dy)) return [];
      const g = gestureBase(merge);
      let guides = [];
      if (snap) { const r = snapMove(layout(g.base, s.loc), s.sel, dx, dy, s.snap); dx = r.dx; dy = r.dy; guides = r.guides; }
      const rx = Math.round(dx), ry = Math.round(dy);
      A.commit(() => {
        const out = moveEls(g.base, s.loc, s.sel, rx, ry);
        // niente spostato con uno scarto vero: il motivo (bloccato…)
        if (out === g.base && (rx || ry)) return { theme: get().hist.present, id: null, reason: refusal('moveEls', g.base, s.loc, s.sel, rx, ry) };
        return out;
      }, merge);
      return guides;
    },

    // riquadro proposto (px della scheda) → riquadro agganciato e intero; a scala = scala intera nei limiti
    resizeSel(id, rect, merge = null, { snap = true, handle = 'se' } = {}) {
      const s = get();
      if (!s.hist || !rect) return [];
      const g = gestureBase(merge);
      const f = findEl(g.base, s.loc, id);
      if (!f) return [];
      let r = rect, guides = [];
      if (SCALED[f.el.type]) r = A.scaledRect(id, rect, g.base);
      else if (snap) { const o = snapResize(layout(g.base, s.loc), id, rect, handle, s.snap); r = o.rect; guides = o.guides; }
      const box = { x: Math.round(r.x), y: Math.round(r.y), w: Math.max(1, Math.round(r.w)), h: Math.max(1, Math.round(r.h)) };
      // stessa misura di partenza (scala arrotondata uguale…) = nessun passo, e nessun avviso a ogni movimento
      A.commit(() => setBox(g.base, s.loc, id, box), merge);
      return guides;
    },

    // elementi a scala (mascotte, batteria, wifi, icona, simbolo): riquadro di base · scala intera, angolo in alto a sinistra fermo
    scaledRect(id, rect, theme = get().hist?.present) {
      const f = theme ? findEl(theme, get().loc, id) : null;
      const sc = f ? SCALED[f.el.type] : null;
      if (!sc) return rect;
      const k = Math.max(1, Math.min(sc[2], Math.round((rect.w / sc[0] + rect.h / sc[1]) / 2)));
      return { x: rect.x, y: rect.y, w: sc[0] * k, h: sc[1] * k };
    },

    // un'operazione sulla selezione: rifiuto spiegato (ops.refusal o reason)
    _onSel(fn, why) {
      const s = get();
      if (!s.hist || !s.sel.length) return false;
      A.commit((t) => {
        const out = fn(t, s.loc, s.sel);
        return out === t && why ? { theme: t, id: null, reason: why(t, s.loc, s.sel) } : out;
      });
      return true;
    },
    deleteSel() { return A._onSel(removeEls, (t, loc, ids) => refusal('removeEls', t, loc, ids)); },
    duplicateSel() { return A._onSel(duplicateEls); },
    groupSel() { return A._onSel(groupEls); },
    ungroupSel() {
      return A._onSel((t, loc, ids) => {
        let cur = t, out = [], reason = null;
        for (const id of ids) {
          const r = ungroup(cur, loc, id);
          if (r.theme === cur) { reason = reason ?? r.reason; out.push(id); continue; }
          cur = r.theme; out = out.concat(r.ids);
        }
        return cur === t ? { theme: t, ids: null, reason } : { theme: cur, ids: out };
      });
    },
    copySel() {
      const s = get();
      if (!s.hist || !s.sel.length) return false;
      const clip = copyEls(s.hist.present, s.loc, s.sel);
      if (!clip.items.length) return false;
      store.set({ clip });
      return true;
    },
    paste() {
      const s = get();
      if (!s.hist || !s.clip) return false;
      A.commit((t) => pasteEls(t, s.loc, s.clip, { parentId: s.scope }));
      return true;
    },
    // tutti al livello aperto, senza bloccati, nascosti e gruppi vuoti
    selectAll() {
      const s = get();
      if (!s.hist) return false;
      store.set({ sel: marquee(layout(s.hist.present, s.loc), { x: -4096, y: -4096, w: 8192, h: 8192 }, s.scope) });
      return true;
    },
    // dentro un gruppo o una lista: i colpi vanno ai figli
    enter(id) {
      const s = get();
      const f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      if (!f || (f.el.type !== 'group' && f.el.type !== 'list')) return false;
      store.set({ scope: id, sel: [] });
      return true;
    },
    // fuori dal gruppo aperto, che resta selezionato
    exitScope() {
      const s = get();
      if (!s.scope) return false;
      const f = s.hist ? findEl(s.hist.present, s.loc, s.scope) : null;
      store.set({ scope: f ? f.parentId : null, sel: f ? [s.scope] : [] });
      return true;
    },
    // un'azione di keyAction; true = fatta (solo allora la pagina chiama preventDefault)
    key(a) {
      const s = get();
      if (!a || !s.hist) return false;
      switch (a.type) {
        case 'nudge': {
          if (!s.sel.length) return false;
          // frecce: mai agganciate (F13); tenendo premuto un passo solo, chiuso al keyup
          A.commit((t) => {
            const out = moveEls(t, s.loc, s.sel, a.dx, a.dy);
            return out === t ? { theme: t, id: null, reason: refusal('moveEls', t, s.loc, s.sel, a.dx, a.dy) } : out;
          }, 'nudge');
          return true;
        }
        case 'delete': return A.deleteSel();
        case 'duplicate': return A.duplicateSel();
        case 'copy': return A.copySel();
        case 'paste': return A.paste();
        case 'group': return A.groupSel();
        case 'ungroup': return A.ungroupSel();
        case 'selectAll': return A.selectAll();
        case 'undo': A.undo(); return true;
        case 'redo': A.redo(); return true;
        case 'enter': return s.sel.length === 1 ? A.enter(s.sel[0]) : false;
        case 'escape':
          if (s.scope) return A.exitScope();
          if (s.sel.length) { store.set({ sel: [] }); return true; }
          return false;
        default: return false;
      }
    },
    // ---------- Aggiungi e Livelli (Task 11) ----------

    // nuovo elemento al centro (center = punto della scheda, di norma il centro della parte visibile della tela),
    // dentro lo schermo, nel contenitore aperto (scope); selezionato; cassetto chiuso
    add(type, center = null) {
      const s = get();
      const def = s.hist ? elementDefaults(type, 0, 0) : null;
      if (!def) return false;
      const ok = A.commit((t) => {
        const first = addEl(t, s.loc, def, { parentId: s.scope });
        if (!first.id) return first;
        const box = layout(first.theme, s.loc).find((b) => b.id === first.id);
        const f = findEl(first.theme, s.loc, first.id);
        if (!box || !f) return first;
        const cx = Number.isFinite(center?.x) ? center.x : 125, cy = Number.isFinite(center?.y) ? center.y : 61;
        const ax = Math.max(0, Math.min(250 - box.w, Math.round(cx - box.w / 2))), ay = Math.max(0, Math.min(122 - box.h, Math.round(cy - box.h / 2)));
        // x e y sono relativi al contenitore: si toglie l'origine degli antenati
        return { theme: updateEl(first.theme, s.loc, first.id, { x: ax - f.ox, y: ay - f.oy }), id: first.id };
      });
      if (ok) store.set({ drawer: null });
      return ok;
    },

    // sposta nel contenitore `parentId` (null = la variante) alla posizione `index` della lista senza l'elemento
    reorderEl(id, parentId, index) {
      const s = get();
      const f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      if (!f || !Number.isInteger(index) || ((parentId ?? null) === f.parentId && index === f.index)) return false;
      const opts = { parentId: parentId ?? null, index };
      return A.commit((t) => {
        const out = reorder(t, s.loc, id, opts);
        return out === t ? { theme: t, id: null, reason: refusal('reorder', t, s.loc, id, opts) } : out;
      });
    },
    toggleHidden(id) {
      const s = get(), f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      return f ? A.commit((t) => updateEl(t, s.loc, id, { hidden: f.el.hidden === true ? undefined : true })) : false;
    },
    toggleLocked(id) {
      const s = get(), f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      return f ? A.commit((t) => updateEl(t, s.loc, id, { locked: f.el.locked === true ? undefined : true })) : false;
    },
    // vuoto toglie `name`; al massimo 32 byte su un confine di carattere
    renameEl(id, name) {
      const s = get(), n = cleanName(name);
      return s.hist ? A.commit((t) => updateEl(t, s.loc, id, { name: n || undefined })) : false;
    },
    // scelta da «Livelli»: l'unico posto dove si scelgono i bloccati; il contenitore del livello diventa quello aperto
    pickLayer(id, add = false) {
      const s = get(), f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      if (!f) return false;
      const sel = add ? (s.sel.includes(id) ? s.sel.filter((x) => x !== id) : [...s.sel, id]) : [id];
      store.set({ sel, scope: add && s.sel.length ? s.scope : f.parentId, notice: '' });
      return true;
    },

    // ---------- Proprietà (Task 12) ----------

    // un campo di fieldsFor; i tasti di fila sullo stesso campo = un passo (chiuso da endGesture all'uscita dal campo).
    // Valori fuori dai limiti riportati dentro; un valore che il servizio rifiuterebbe non entra (motivo in notice).
    setProp(id, key, value) {
      const s = get();
      const f = s.hist ? findEl(s.hist.present, s.loc, id) : null;
      const field = f ? fieldsFor(f.el.type).find((x) => x.key === key) : null;
      if (!field) return false;
      const no = (reason) => { store.set({ notice: reason }); return false; };
      if (f.el.locked === true || f.trail.some((a) => a.locked === true)) return no(LOCKED);
      const inRow = f.trail.some((a) => a.type === 'list');
      let v = value;
      switch (field.kind) {
        case 'int': {
          const empty = v === '' || v === null || v === undefined || (typeof v === 'string' && !v.trim());
          if (empty && field.optional) { v = undefined; break; }
          // solo interi decimali scritti per esteso (niente «1e2», «0x10»)
          const t = typeof v === 'string' ? v.trim().replace('−', '-') : null;
          const n = t !== null ? (/^-?\d+$/.test(t) ? Number(t) : NaN) : v;
          if (empty || typeof n !== 'number' || !Number.isFinite(n)) return no(`${field.label}: serve un numero da ${field.min} a ${field.max}.`);
          v = Math.max(field.min, Math.min(field.max, Math.round(n)));
          break;
        }
        case 'select':
          if (!field.options.some((o) => o.value === v)) return false;
          if (key === 'ref' && v === 'row' && !inRow) return no(ROW);
          if (v === field.def) v = undefined;
          break;
        case 'bool':
          v = v === true;
          if (v === field.def) v = undefined;
          break;
        case 'template': {
          const why = templateProblem(v, inRow);
          if (why) return no(why);
          break;
        }
        case 'font': {
          const k = typeof v === 'string' ? v : isObj(v) ? `${v.family}:${v.size}:${v.weight}` : '';
          const c = FONT_CHOICES.find((x) => x.key === k);
          if (!c) return no('Carattere che la scheda non ha.');
          v = { ...c.font };
          break;
        }
        case 'conds': {
          const list = Array.isArray(v) ? v : [];
          if (!list.every(condOk)) return no('Condizione non valida.');
          // con quelle dei gruppi sopra (le righe di una lista non ereditano)
          const li = f.trail.map((a) => a.type).lastIndexOf('list');
          const inherited = f.trail.slice(li + 1).reduce((n, a) => n + condCount(a.visibleIf), 0);
          // un gruppo passa le sue a ogni figlio: conta anche il discendente che ne ha di più (parse.go)
          const below = f.el.type === 'group' ? deepestConds(f.el.children) : 0;
          if (condCount(list) + inherited + below > 8) {
            const why = [inherited ? `le ${inherited} dei gruppi che lo contengono` : '', below ? `le ${below} di un elemento dentro il gruppo` : ''].filter(Boolean).join(' e ');
            return no(`Al massimo 8 condizioni${why ? ', contando ' + why : ''}.`);
          }
          v = list.length ? list.map((c) => JSON.parse(JSON.stringify(c))) : undefined;
          break;
        }
        default: return false;
      }
      return A.commit((t) => updateEl(t, s.loc, id, { [key]: v }), 'prop:' + id + ':' + key);
    },

    // when/ref della variante aperta
    setVariantProp(patch) {
      const s = get();
      if (!s.hist || !isObj(patch)) return false;
      return A.commit((t) => updateVariant(t, s.loc.screen, s.loc.variant, patch));
    },

    // scarti di align/distribute: un passo solo, senza gli scarti nulli (nessun passo vuoto), mezzi punti arrotondati
    _moveMap(map, none) {
      const s = get();
      if (!s.hist || !s.sel.length) return false;
      const t0 = s.hist.present;
      const ids = [...map.keys()];
      const moves = [];
      for (const [id, d] of map) {
        const dx = Math.round(d.dx), dy = Math.round(d.dy);
        if (!dx && !dy) continue;
        // un figlio di un elemento che si sposta già si muove con lui
        const f = findEl(t0, s.loc, id);
        if (f && f.trail.some((a) => ids.includes(a.id))) continue;
        moves.push([id, dx, dy]);
      }
      if (!moves.length) {
        const boxes = layout(t0, s.loc);
        const allLocked = s.sel.every((id) => boxes.find((b) => b.id === id)?.locked);
        store.set({ notice: allLocked ? LOCKED : none });
        return false;
      }
      return A.commit((t) => moves.reduce((cur, [id, dx, dy]) => moveEls(cur, s.loc, [id], dx, dy), t));
    },
    // left, hcenter, right, top, vcenter, bottom; uno solo = rispetto allo schermo
    alignSel(mode) {
      const s = get();
      if (!s.hist || !s.sel.length) return false;
      return A._moveMap(align(layout(s.hist.present, s.loc), s.sel, mode), 'Già allineati.');
    },
    // h, v: spazi uguali fra il primo e l'ultimo; servono almeno 3 elementi non bloccati
    distributeSel(axis) {
      const s = get();
      if (!s.hist || !s.sel.length) return false;
      const boxes = layout(s.hist.present, s.loc);
      if (boxes.filter((b) => s.sel.includes(b.id) && !b.locked).length < 3) { store.set({ notice: 'Per distribuire servono almeno 3 elementi non bloccati.' }); return false; }
      return A._moveMap(distribute(boxes, s.sel, axis), 'Già distribuiti.');
    },
    // griglia 1/2/4/8 e aggancio agli altri elementi
    setSnap({ grid, others } = {}) {
      const cur = get().snap, next = { ...cur };
      if (GRIDS.includes(grid)) next.grid = grid;
      if (typeof others === 'boolean') next.others = others;
      if (next.grid === cur.grid && next.others === cur.others) return false;
      store.set({ snap: next });
      return true;
    },

    // pizzico: lo zoom torna intero alla fine del gesto
    pinchZoom(z0, factor) { if (Number.isFinite(factor) && factor > 0) A.setZoom(z0 * factor); },
    toggleMulti() { store.set({ touchMulti: !get().touchMulti }); },

    undo() { A._step(undo); },
    redo() { A._step(redo); },
    _step(op) {
      const s = get();
      if (!s.hist) return;
      const hist = op(s.hist);
      if (hist === s.hist) return;
      const n = variantCount(hist.present, s.loc.screen);
      const loc = s.loc.variant < n || n === 0 ? s.loc : { ...s.loc, variant: n - 1 };
      setHist(hist, { loc, sel: pruneIds(hist.present, loc, s.sel), notice: '' });
    },

    select(ids) {
      const s = get();
      if (!s.hist) return;
      store.set({ sel: [...new Set(pruneIds(s.hist.present, s.loc, ids))] });
    },

    setZoom(z) {
      const n = Math.round(Number(z));
      if (!Number.isFinite(n)) return;
      store.set({ zoom: Math.max(1, Math.min(6, n)) });
    },

    setDrawer(d) { store.set({ drawer: d === 'left' || d === 'right' ? d : null }); },

    // per i task seguenti (salvataggio coi timer, animazione): orologio e timer iniettati
    _clock: { now, later, cancel },
  };
  return A;
}
