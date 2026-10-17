// Dati e regole presi dal simulatore (reference/calamaio-os-browser/assets/calamaio-test.js):
// STATES, HOME_LABEL, ANIM_LABEL, SPEED_LABEL, ICONS, DEVICES (solo i campi che servono qui),
// urgentIdx, pose, ago, hm, resetLabel. Testi identici al simulatore.

export const STATES = {
  working: { label: 'LAVORA', long: 'Sta lavorando', prio: 3 },
  permission: { label: 'PERMESSO', long: 'Chiede un permesso', prio: 6 },
  input: { label: 'ATTENDE TE', long: 'Aspetta una risposta', prio: 5 },
  error: { label: 'ERRORE', long: 'Errore API', prio: 4 },
  compact: { label: 'RIORDINA', long: 'Riordina la memoria', prio: 3 },
  done: { label: 'FINITO', long: 'Turno completato', prio: 2 },
  idle: { label: 'IN PAUSA', long: 'Sessione aperta', prio: 1 }
};

// Etichette delle pose speciali (guida e brief §11).
export const POSE_LABEL = {
  working: 'LAVORA', permission: 'PERMESSO', input: 'ATTENDE TE', error: 'ERRORE', compact: 'RIORDINA',
  done: 'FINITO', idle: 'IN PAUSA', tired: 'STANCA', stuffy: 'ARIA VIZIATA', run: 'CORRE'
};

export const HOME_LABEL = { goccia: 'Goccia', righe: 'Righe', contatori: 'Contatori', una: 'Una alla volta' };
export const ANIM_LABEL = { auto: 'Auto', raffiche: 'A raffiche', continue: 'Continue', spente: 'Spente' };
export const SPEED_LABEL = { profilo: 'Profilo', massima: 'Massima' };
// Valori di Silenzia come li mostra il menu del simulatore (no / 1 ora / fino alle 8).
export const MUTE_LABEL = { off: 'no', '1h': '1 ora', night: 'fino alle 8' };

export const ICONS = {
  done: ['.......', '......#', '.....##', '#...##.', '##.##..', '.###...', '..#....'],
  error: ['#.....#', '##...##', '.##.##.', '..###..', '.##.##.', '##...##', '#.....#'],
  input: ['.#####.', '##...##', '.....##', '...###.', '..##...', '.......', '..##...'],
  permission: ['..##...', '..##...', '..##...', '..##...', '..##...', '.......', '..##...'],
  idle: ['#######', '....##.', '...##..', '..##...', '.##....', '#######', '.......'],
  compact: ['#######', '.#####.', '.......', '..###..', '.......', '...#...', '...#...'],
  working0: ['#######', '.#####.', '..###..', '...#...', '..#.#..', '.#...#.', '#######'],
  working1: ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######']
};

export const DEVICES = {
  heltec213: { name: 'Heltec Wireless Paper', size: '2,13″', W: 250, H: 122, cls: 'S', fps: 2, fullEvery: 40 },
  t5213: { name: 'LilyGO T5 V2.4', size: '2,13″', W: 250, H: 122, cls: 'S', fps: 2, fullEvery: 40 },
  crow42: { name: 'CrowPanel ESP32‑S3', size: '4,2″', W: 400, H: 300, cls: 'M', fps: 2, fullEvery: 60 },
  crow579: { name: 'CrowPanel ESP32‑S3', size: '5,79″', W: 792, H: 272, cls: 'W', fps: 2, fullEvery: 60 },
  t547: { name: 'LilyGO T5 E‑Paper S3', size: '4,7″', W: 960, H: 540, cls: 'L', fps: 4, fullEvery: 60 },
  reterm75: { name: 'Seeed reTerminal E1001', size: '7,5″', W: 800, H: 480, cls: 'L', fps: 1, fullEvery: 30, buzzer: true, led: true }
};

export function deviceName(id) {
  const d = DEVICES[id];
  return d ? d.name + ' ' + d.size : (id || 'modello sconosciuto');
}

export const isAttn = (s) => s.state === 'permission' || s.state === 'input' || s.state === 'error';

export function urgentIdx(sessions) {
  let b = 0;
  sessions.forEach((s, i) => { if (prio(s) > prio(sessions[b])) b = i; });
  return b;
}
const prio = (s) => (STATES[s.state] ? STATES[s.state].prio : 0);

// Come pose() del simulatore; ARIA VIZIATA richiede la CO₂, che l'API non manda.
export function pose(limits, s) {
  if (limits && limits.h5 >= 90 && (s.state === 'working' || s.state === 'done' || s.state === 'idle')) return 'tired';
  return STATES[s.state] ? s.state : 'idle';
}

const pad2 = (n) => String(n).padStart(2, '0');
export const hm = (d) => pad2(d.getHours()) + ':' + pad2(d.getMinutes());

export function ago(t) {
  const m = Math.max(0, Math.round((Date.now() - t) / 60000));
  return m < 60 ? m + ' min' : Math.floor(m / 60) + ' h ' + pad2(m % 60);
}

export function resetLabel(ts) {
  const d = new Date(ts);
  if (d.toDateString() === new Date().toDateString()) return 'si azzera alle ' + hm(d);
  try { return 'si azzera ' + d.toLocaleDateString('it-IT', { weekday: 'long' }) + ' alle ' + hm(d); } catch (e) { return 'si azzera alle ' + hm(d); }
}

export function extras(s) {
  const a = [];
  if (s.subs) a.push('+' + s.subs + (s.subs === 1 ? ' agente' : ' agenti'));
  if (s.test) a.push(s.test === 'ok' ? 'test ✓' : s.test === 'ko' ? 'test ✗' : 'test in corso');
  return a;
}

export function stateText(s) {
  if (s.state === 'working') return s.detail || STATES.working.long;
  if (s.state === 'done' && s.summary) return s.summary;
  const st = STATES[s.state];
  return st ? st.long : s.state;
}

// Giornata di lavoro (la tiene la scheda): «mancano 6 h 25» come sul display, minuti interi per difetto
// (6 h 25 min 59 s → «6 h 25»), come `calamaio workday status`.
export function workdayLeft(end, now = Date.now()) {
  const ms = end - now;
  if (ms <= 0) return 'finita';
  // per eccesso, come sul display (core/workday.cpp)
  const m = Math.max(0, Math.ceil(ms / 60000));
  return m < 60 ? 'mancano ' + m + ' min' : 'mancano ' + Math.floor(m / 60) + ' h ' + pad2(m % 60);
}

// «mancano 6 h 25, fino alle 18:00» per una giornata accesa (workday di /api/status); null se spenta.
export function workdayText(w, now = Date.now()) {
  if (!w || !w.on) return null;
  if (w.end === null || w.end === undefined) return 'in corso';
  const left = workdayLeft(w.end, now), at = hm(new Date(w.end));
  return left === 'finita' ? 'finita alle ' + at : left + ', fino alle ' + at;
}
