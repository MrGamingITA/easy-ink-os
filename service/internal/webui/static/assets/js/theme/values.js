// Valori, condizioni e sessione di riferimento dei temi come core/theme_values.cpp (docs/TEMI.md §3–§4) e le funzioni
// del modello che usano (stateText, lineFor, metaLine, ago, autonomyText, workLine, pose…: core/model.cpp, clock.cpp,
// workday.cpp). Le lunghezze sono in byte UTF-8 come nei buffer del C++. Un surrogato isolato diventa U+FFFD (3 byte),
// come lo ricodifica TextEncoder a valle: così il conto dei byte è quello dei byte che il C++ riceverebbe.
export const STATES = ['working', 'permission', 'input', 'error', 'compact', 'done', 'idle'];
const INFO = {
  working: ['LAVORA', 'Sta lavorando', 3], permission: ['PERMESSO', 'Chiede un permesso', 6], input: ['ATTENDE TE', 'Aspetta una risposta', 5],
  error: ['ERRORE', 'Errore API', 4], compact: ['RIORDINA', 'Riordina la memoria', 3], done: ['FINITO', 'Turno completato', 2], idle: ['IN PAUSA', 'Sessione aperta', 1],
};
const SNAMES = ['nome', 'stato', 'attività', 'riassunto', 'meta', 'contesto', 'task', 'da', 'sub', 'test'];
const GNAMES = [['ora', 0x80], ['data', 0x82], ['giorno', 0x83], ['sessioni', 0x84], ['inattesa', 0x85], ['batteria', 0x86], ['autonomia', 0x87],
  ['wifi', 0x88], ['pc', 0x89], ['giornata.mancano', 0x8a], ['giornata.fine', 0x8b], ['oggi.task', 0x8c], ['oggi.righe', 0x8d], ['oggi.costo', 0x8e],
  ['limiti.5h', 0x8f], ['limiti.7g', 0x90], ['mascotte.livello', 0x91], ['mascotte.xp', 0x92], ['mascotte.energia', 0x93], ['co2', 0x94], ['versione', 0x95]];
export const VALUES = Object.freeze(Object.assign(Object.create(null), Object.fromEntries([...SNAMES.flatMap((n, i) => [['sessione.' + n, i], ['riga.' + n, 0x40 + i]]), ...GNAMES])));
const WEEKDAYS = ['domenica', 'lunedì', 'martedì', 'mercoledì', 'giovedì', 'venerdì', 'sabato'];
const MONTHS = ['gennaio', 'febbraio', 'marzo', 'aprile', 'maggio', 'giugno', 'luglio', 'agosto', 'settembre', 'ottobre', 'novembre', 'dicembre'];
const DAY = 86400000;
const SEL_MS = 15000;  // kSelMs
const jsRound = (v) => Math.floor(v + 0.5);
const pad2 = (v) => (v >= 0 && v < 10 ? '0' : '') + v;
const fdiv = (a, b) => Math.floor(a / b);
const fmod = (a, b) => a - fdiv(a, b) * b;

// civilFrom() di clock.cpp (H. Hinnant), interi come int64
function civil(l) {
  const days = fdiv(l, DAY), ms = fmod(l, DAY);
  const z = days + 719468, era = fdiv(z, 146097), doe = z - era * 146097;
  const yoe = Math.trunc((doe - Math.trunc(doe / 1460) + Math.trunc(doe / 36524) - Math.trunc(doe / 146096)) / 365);
  const doy = doe - (365 * yoe + Math.trunc(yoe / 4) - Math.trunc(yoe / 100)), mp = Math.trunc((5 * doy + 2) / 153);
  const mo = mp < 10 ? mp + 3 : mp - 9;
  return { y: yoe + era * 400 + (mo <= 2 ? 1 : 0), mo, d: doy - Math.trunc((153 * mp + 2) / 5) + 1,
    h: Math.trunc(ms / 3600000), mi: Math.trunc(ms / 60000) % 60, s: Math.trunc(ms / 1000) % 60, wd: fmod(days + 4, 7) };
}
const hmLocal = (l) => { const c = civil(l); return pad2(c.h) + ':' + pad2(c.mi); };
const minHm = (mins) => { mins = Math.max(0, mins); return pad2(Math.trunc(mins / 60)) + ':' + pad2(mins % 60); };

// byte UTF-8 di un code point (surrogato isolato = U+FFFD, 3 byte)
const cpBytes = (c) => (c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4);
// Al massimo maxBytes byte UTF-8 senza spezzare un carattere (utf8Fit del C++); i surrogati isolati diventano U+FFFD.
export function utf8Cut(s, maxBytes) {
  let n = 0, out = '';
  for (let ch of s) {
    let c = ch.codePointAt(0);
    if (c >= 0xd800 && c <= 0xdfff) { ch = '�'; c = 0xfffd; }
    const k = cpBytes(c);
    if (n + k > maxBytes) break;
    n += k;
    out += ch;
  }
  return out;
}
const utf8Len = (s) => { let n = 0; for (const ch of s) { const c = ch.codePointAt(0); n += c >= 0xd800 && c <= 0xdfff ? 3 : cpBytes(c); } return n; };

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
function merge(a, b) {
  const out = { ...a };
  for (const [k, v] of Object.entries(b || {})) if (k !== '__proto__') out[k] = isObj(v) && isObj(a[k]) ? merge(a[k], v) : v;
  return out;
}
// i64() di apply_state.cpp: llround (metà lontano dallo zero) sui numeri, def sul resto
const int = (v, def = 0) => (typeof v === 'number' && Number.isFinite(v) ? Math.sign(v) * Math.round(Math.abs(v)) || 0 : def);
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const num = (v, d = 0) => (typeof v === 'number' && !Number.isNaN(v) ? v : d);
const str = (v, cap) => utf8Cut(typeof v === 'string' ? v : '', cap - 1);  // char[cap] del C++
// text() di apply_state.cpp: i caratteri di controllo diventano spazi, poi il taglio UTF-8
const text = (v, cap) => str(typeof v === 'string' ? v.replace(/[\x00-\x1f]/g, ' ') : '', cap);
const numOrNull = (v) => (typeof v === 'number' && !Number.isNaN(v) ? v : null);

// Modello del renderer: fusione profonda di `over` su `base`, poi i tipi e le capienze del C++ (model.h).
export function makeModel(base, over = {}) {
  const s = merge(isObj(base) ? base : {}, isObj(over) ? over : {});
  const o = (v) => (isObj(v) ? v : {});
  const now = num(s.now), sys = o(s.sys), set = o(s.set), work = o(s.work), st = o(s.stats), lim = o(s.limits);
  const pet = o(s.pet), bat = o(s.bat), cal = o(bat.cal), mix = Array.isArray(bat.mix) ? bat.mix : [];
  const sel = num(s.sel, -1);
  return {
    now, local: num(s.local, now), frame: int(s.frame), wallOk: !!(s.wallOk ?? true), fw: typeof s.fw === 'string' ? s.fw : '',
    ui: { sel: Math.max(0, int(sel)), selUntil: sel >= 0 ? now + SEL_MS : 0 },
    sys: { batt: num(sys.batt, 78), usb: !!sys.usb, wifi: !!(sys.wifi ?? true), ssid: str(sys.ssid, 33), rssi: int(sys.rssi ?? -58),
      pc: !!(sys.pc ?? true), pcOff: !!sys.pcOff, pcHost: str(sys.pcHost, 48) },
    set: { night: !!set.night, game: !!set.game, workStart: int(set.workStart ?? 540), workEnd: int(set.workEnd ?? 1080),
      defAvatar: typeof set.defAvatar === 'string' ? set.defAvatar : 'goccia' },
    sessions: (Array.isArray(s.sessions) ? s.sessions : []).filter(isObj).slice(0, 6).map((x) => ({
      name: text(x.name, 32), state: STATES.includes(x.state) ? x.state : 'idle', detail: text(x.detail, 96), summary: text(x.summary, 128),
      since: int(x.since, now - int(x.ago)), ctx: clamp(int(x.ctx), 0, 100), subs: clamp(int(x.subs), 0, 99), test: x.test === 'ok' || x.test === 'ko' ? x.test : null,
      done: int(x.done), total: int(x.total), avatar: typeof x.avatar === 'string' ? x.avatar : '' })),
    work: { on: !!work.on, endLocal: num(work.endLocal), fromLocal: num(work.fromLocal), doneUntilLocal: num(work.doneUntilLocal) },
    stats: { tasks: clamp(int(st.tasks), 0, 1000000), linesAdd: int(st.linesAdd), linesDel: int(st.linesDel), cost: num(st.cost) },
    limits: { h5: numOrNull(lim.h5), d7: numOrNull(lim.d7) },
    pet: { known: !!pet.known, on: !!pet.on, level: int(pet.level), xp: int(pet.xp), xpLevelStart: int(pet.xpLevelStart), xpNext: int(pet.xpNext),
      energy: int(pet.energy), tired: !!pet.tired, wear: typeof pet.wear === 'string' ? pet.wear : '' },
    co2: numOrNull(s.co2),
    bat: { cal: { valid: !!cal.valid, active: num(cal.active), idle: num(cal.idle) }, hasSleep: !!bat.hasSleep, sleep: num(bat.sleep),
      mix: [int(mix[0]), int(mix[1]), int(mix[2])], trend: num(bat.trend) },
  };
}

const prio = (s) => INFO[s.state][2];
const isAttn = (s) => s.state === 'permission' || s.state === 'input' || s.state === 'error';
export const urgentIdx = (m) => { let b = 0; m.sessions.forEach((s, k) => { if (prio(s) > prio(m.sessions[b])) b = k; }); return b; };
const sIdx = (m) => (m.ui.selUntil > m.now ? m.ui.sel : urgentIdx(m));
export const petOn = (m) => m.set.game && m.pet.on;
export const permWaiting = (m) => m.sessions.filter((s) => s.state === 'permission').length;
// ref: 0 urgente, 1 selezionata (nel Dettaglio la sessione aperta, altrove sIdx come la Home del C++); −1 senza sessioni
export function refIndex(m, screen, ref) {
  const n = m.sessions.length;
  if (n <= 0) return -1;
  const k = ref === 0 ? urgentIdx(m) : screen === 'detail' ? m.ui.sel : sIdx(m);
  return Math.min(Math.max(k, 0), n - 1);
}
export function whenTrue(m, w) { const n = m.sessions.length; return w === 1 ? n === 0 : w === 2 ? n === 1 : w === 3 ? n >= 2 : true; }
// ref dell'elemento: 0 urgente, 1 selezionata, 2 riga, 3 nessuna, 4 quella della variante (o la riga dentro una lista)
export function refSession(m, cx, ref, row) {
  if (ref === 2) return row || null;
  if (ref === 3) return null;
  if (ref === 0 || ref === 1) { const k = refIndex(m, cx.screen, ref); return k < 0 ? null : m.sessions[k]; }
  if (row) return row;
  return cx.ref < 0 || cx.ref >= m.sessions.length ? null : m.sessions[cx.ref];
}
const calm = (s) => s.state === 'working' || s.state === 'done' || s.state === 'idle';
export function pose(m, s) {
  if (m.limits.h5 !== null && m.limits.h5 >= 90 && calm(s)) return 'tired';
  if (m.set.game && m.pet.on && m.pet.tired && calm(s)) return 'tired';
  if (m.co2 !== null && m.co2 > 1000 && !isAttn(s)) return 'stuffy';
  return s.state;
}
const stateText = (s) => (s.state === 'working' ? s.detail : s.state === 'done' && s.summary ? s.summary : INFO[s.state][1]);
const lineFor = (s) => (s.state === 'done' && s.summary ? s.summary : s.detail);
const roundDiv = (a, b) => fdiv(2 * a + b, 2 * b);
function ago(now, t) { const mins = Math.max(0, roundDiv(now - t, 60000)); return mins < 60 ? mins + ' min' : Math.trunc(mins / 60) + ' h ' + pad2(mins % 60); }
function extras(s, sep) {
  const e = [];
  if (s.subs) e.push('+' + s.subs + (s.subs === 1 ? ' agente' : ' agenti'));
  if (s.test) e.push(s.test === 'ok' ? 'test ✓' : 'test ✗');
  if (s.ctx >= 80) e.push('contesto ' + s.ctx + '%');
  return utf8Cut(e.join(sep), 63);  // ex[64] in metaLine
}
function metaLine(s, now) {
  const sep = '  ·  ';
  let out = (s.total ? s.done + '/' + s.total + ' task' : 'senza task') + sep + ago(now, s.since);
  const ex = extras(s, sep);
  if (ex) out += sep + ex;
  return out + sep + lineFor(s);
}
const hoursText = (h) => (h >= 48 ? 'circa ' + jsRound(h / 24) + ' giorni' : 'circa ' + (h < 1 ? 1 : jsRound(h)) + ' ore');
function battRate(m) {
  const c = m.bat.cal, x = m.bat.mix, tot = x[0] + x[1] + x[2];
  if (!c.valid) return 0;
  return tot > 0 ? (x[0] * c.active + x[1] * c.idle + x[2] * (m.bat.hasSleep ? m.bat.sleep : 0.3)) / tot : 0;
}
function autonomyText(m) {
  const r = battRate(m);
  if (m.bat.cal.valid && r > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / r) + ' con il tuo uso';
  if (!m.sys.usb && m.bat.trend > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / m.bat.trend) + ' (dalla scarica)';
  return m.sys.usb ? 'Autonomia: calibra per una stima' : 'Autonomia: stima in corso';
}
const workDone = (m, local) => m.work.doneUntilLocal > local;
function workLine(m, local) {
  if (m.work.on) {
    const d = m.work.endLocal - local, n = d <= 0 ? 0 : Math.trunc((d + 59999) / 60000);
    return n >= 60 ? Math.trunc(n / 60) + ' h ' + pad2(n % 60) : n + ' min';
  }
  return workDone(m, local) ? 'finita' : '';
}
// workFrac() di workday.cpp: avanzamento della giornata 0–1
export function workFrac(m, local = m.local) {
  const w = m.work;
  let a = w.endLocal - fmod(w.endLocal, DAY) + m.set.workStart * 60000;
  if (a >= w.endLocal) a = w.fromLocal;
  if (w.endLocal <= a) return 1;
  const f = (local - a) / (w.endLocal - a);
  return f < 0 ? 0 : f > 1 ? 1 : f;
}
const pct = (v) => (v === null ? '—' : jsRound(v) + '%');

// snprintf("%.2f") di C: valore decimale esatto del double, e sui pari merito esatti (solo i multipli dispari di 1/8)
// la cifra pari. toFixed arrotonda anch'esso il valore esatto ma sui pari merito sceglie il numero più grande.
function printf2(x) {
  if (Number.isNaN(x)) return 'nan';
  const neg = x < 0 || Object.is(x, -0), a = Math.abs(x);
  if (a === Infinity) return neg ? '-inf' : 'inf';
  let s;
  if (a >= 1e21) s = BigInt(a).toString() + '.00';
  else if (Number.isInteger(a * 8) && !Number.isInteger(a * 4)) {
    // a·100 = c + ½ esatto; in BigInt anche oltre 2^46, dove a·100 non sta più in un double
    let c = (BigInt(a * 8) * 25n - 1n) / 2n;
    if (c % 2n) c++;
    s = (c / 100n) + '.' + String(c % 100n).padStart(2, '0');
  } else s = a.toFixed(2);
  return (neg ? '-' : '') + s;
}

function sessionValue(k, s, now) {
  switch (k) {
    case 0: return s.name;
    case 1: return stateText(s);
    case 2: return lineFor(s);
    case 3: return s.summary;
    case 4: return metaLine(s, now);
    case 5: return s.ctx + '%';
    case 6: return s.total ? s.done + '/' + s.total : '';
    case 7: return ago(now, s.since);
    case 8: return s.subs ? String(s.subs) : '';
    case 9: return s.test ? (s.test === 'ok' ? '✓' : '✗') : '';
  }
  return '';
}

function generalValue(id, cx) {
  const m = cx.m, c = civil(cx.local);
  switch (id) {
    case 0x80: return m.wallOk ? hmLocal(cx.local) : '--:--';
    case 0x81: return m.wallOk ? pad2(c.h) + ':' + pad2(c.mi) + ':' + pad2(c.s) : '--:--:--';
    case 0x82: return m.wallOk ? WEEKDAYS[c.wd] + ' ' + c.d + ' ' + MONTHS[c.mo - 1] : '';
    case 0x83: return m.wallOk ? WEEKDAYS[c.wd] : '';
    case 0x84: return String(m.sessions.length);
    case 0x85: return String(permWaiting(m));
    case 0x86: return jsRound(m.sys.batt) + '%';
    case 0x87: return autonomyText(m);
    case 0x88: return m.sys.wifi ? m.sys.ssid : '';
    case 0x89: return m.sys.pcHost || 'calamaio.local';
    case 0x8a: return workLine(m, cx.local);
    case 0x8b: return m.work.on || workDone(m, cx.local) ? hmLocal(m.work.endLocal) : minHm(m.set.workEnd);
    case 0x8c: return String(m.stats.tasks);
    case 0x8d: return '+' + m.stats.linesAdd + ' −' + m.stats.linesDel;
    case 0x8e: return utf8Cut(printf2(m.stats.cost) + ' $', 63).replaceAll('.', ',');  // t[64]
    case 0x8f: return pct(m.limits.h5);
    case 0x90: return pct(m.limits.d7);
    case 0x91: return m.pet.known ? String(m.pet.level) : '—';
    case 0x92: return m.pet.known ? m.pet.xp - m.pet.xpLevelStart + '/' + (m.pet.xpNext - m.pet.xpLevelStart) + ' XP' : '';
    case 0x93: return m.pet.known ? m.pet.energy + '%' : '';
    case 0x94: return m.co2 !== null ? jsRound(m.co2) + ' ppm' : '';
    case 0x95: return m.fw;  // versione della scheda, la passa il chiamante
  }
  return '';
}

// Testo di un valore (buffer da 256 byte come nel C++). row = la sessione della riga per i riga.*.
export function valueText(id, cx, row = null) {
  const m = cx.m;
  if (id < 0x80) {
    const s = id >= 0x40 ? row : cx.ref >= 0 && cx.ref < m.sessions.length ? m.sessions[cx.ref] : null;
    return s ? utf8Cut(sessionValue(id & 0x3f, s, cx.now), 255) : '';
  }
  return utf8Cut(generalValue(id, cx), 255);
}

export function condTrue(op, arg, cx, subject) {
  const m = cx.m, want = arg !== 0;
  switch (op) {
    case 1: return (permWaiting(m) > 0) === want;
    case 2: return m.sys.batt < arg;  // valore grezzo, non quello arrotondato del testo
    case 3: return m.sys.usb === want;
    case 4: return m.set.night === want;
    case 5: return m.sys.pc === want;
    case 6: return m.work.on === want;
    case 7: return petOn(m) === want;
    case 8: return m.sessions.length >= arg;
    case 9: return m.sessions.length <= arg;
    case 10: return !!subject && STATES.indexOf(subject.state) === arg;
    case 11: return (((cx.frame % 2) + 2) % 2) === arg;
  }
  return false;
}
export const condsTrue = (conds, cx, subject) => conds.every(([op, arg]) => condTrue(op, arg, cx, subject));

// caseMap() del C++: ASCII e il blocco U+00C0–U+00FE (byte 0xC3 …), senza × ÷; ß e ÿ restano.
const mapCase = (s, up) => Array.from(s, (ch) => {
  const c = ch.charCodeAt(0);
  if (up && ((c >= 0x61 && c <= 0x7a) || (c >= 0xe0 && c <= 0xfe && c !== 0xf7))) return String.fromCharCode(c - 0x20);
  if (!up && ((c >= 0x41 && c <= 0x5a) || (c >= 0xc0 && c <= 0xde && c !== 0xd7))) return String.fromCharCode(c + 0x20);
  return ch;
}).join('');

// Testo finale di un elemento: al massimo 255 byte UTF-8, troncato su un confine di carattere (append() del C++).
export function expand(segs, cx, row, upperAll) {
  let out = '', n = 0;
  for (const sg of segs) {
    let p = sg.lit !== undefined ? utf8Cut(sg.lit, 255) : valueText(sg.val, cx, row);
    if (sg.filter === 1) p = mapCase(p, true);
    else if (sg.filter === 2) p = mapCase(p, false);
    const add = utf8Cut(p, 255 - n);
    out += add;
    n += utf8Len(add);
  }
  return upperAll ? mapCase(out, true) : out;
}
