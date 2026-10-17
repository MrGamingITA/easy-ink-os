// Esegue un caso del banco di parità con values.js della webapp; le risposte hanno la forma di quelle di harness.cpp.
//   caso = { m: dati del modello (forma di makeModel), q: [domanda…] }
//   domande: ['v', screen, vref, frame, id, row] · ['c', op, arg, frame, subject] · ['r', screen, ref]
//            ['s', screen, vref, ref, row] · ['w', when] · ['p', k] · ['x', screen, vref, frame, row, upper, segs] · ['f']
//   screen: 1 home, 2 detail (Screen del C++); row e subject: indice della sessione o −1.
import * as V from '../../service/internal/webui/static/assets/js/theme/values.js';

const SCREENS = { 1: 'home', 2: 'detail' };
export const hex = (s) => (s === '' ? '-' : Buffer.from(s, 'utf8').toString('hex'));
const at = (m, k) => (k >= 0 && k < m.sessions.length ? m.sessions[k] : null);
const ctx = (m, screen, vref, frame) => ({ m, now: m.now, local: m.local, frame, screen: SCREENS[screen], ref: V.refIndex(m, SCREENS[screen], vref) });
function dbl(f) { const b = Buffer.alloc(8); b.writeDoubleLE(f); return b.reverse().toString('hex'); }

export function runJs(c) {
  const m = V.makeModel(c.m);
  return c.q.map((q) => {
    switch (q[0]) {
      case 'v': return hex(V.valueText(q[4], ctx(m, q[1], q[2], q[3]), at(m, q[5])));
      case 'c': return V.condTrue(q[1], q[2], ctx(m, 1, 1, q[3]), at(m, q[4])) ? '1' : '0';
      case 'r': return String(V.refIndex(m, SCREENS[q[1]], q[2]));
      case 's': { const s = V.refSession(m, ctx(m, q[1], q[2], 0), q[3], at(m, q[4])); return String(s ? m.sessions.indexOf(s) : -1); }
      case 'w': return V.whenTrue(m, q[1]) ? '1' : '0';
      case 'p': return V.pose(m, m.sessions[q[1]]);
      case 'x': return hex(V.expand(q[6], ctx(m, q[1], q[2], q[3]), at(m, q[4]), !!q[5]));
      case 'f': return dbl(V.workFrac(m));
    }
    throw new Error('domanda sconosciuta ' + q[0]);
  });
}
