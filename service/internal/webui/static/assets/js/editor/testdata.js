// Dati di prova dell'editor: lo scenario di base e le sessioni di protocol/themes/scenarios.json (copiati: la prova li confronta).
import { makeModel } from '../theme/values.js';

export const BASE = {"now": 1791707400000, "local": 1791714600000, "frame": 0, "wallOk": true, "fw": "", "sys": {"batt": 78, "usb": false, "wifi": true, "ssid": "CasaWiFi", "rssi": -58, "pc": true, "pcOff": false, "pcHost": ""}, "set": {"night": false, "game": false, "workStart": 540, "workEnd": 1080, "defAvatar": "goccia"}, "sessions": [], "stats": {"tasks": 4, "linesAdd": 120, "linesDel": 30, "cost": 1.2}, "limits": {"h5": null, "d7": null}};

export const SESSIONS = {
  0: [],
  1: [{"name": "api-server", "state": "working", "detail": "Bash: npm run test", "ago": 720000, "ctx": 42, "done": 3, "total": 7, "avatar": "gatto"}],
  6: [{"name": "api-server", "state": "working", "detail": "Bash: npm run test", "ago": 720000, "ctx": 42, "done": 3, "total": 7, "avatar": "gatto"}, {"name": "web-app", "state": "permission", "detail": "Edit: src/app.ts", "ago": 60000, "ctx": 81, "subs": 2, "avatar": "demone"}, {"name": "firmware-esp32-progetto-lungo", "state": "input", "detail": "Quale scheda uso per la prova di domani mattina?", "ago": 4500000, "avatar": "calamaro"}, {"name": "docs-site", "state": "done", "summary": "Guida di installazione aggiornata e 4 link rotti corretti.", "ago": 300000, "done": 6, "total": 6, "test": "ok"}, {"name": "ricerca", "state": "error", "detail": "Errore API 529", "ago": 30000, "test": "ko"}, {"name": "note", "state": "idle", "ago": 7200000}],
};

const clone = (v) => JSON.parse(JSON.stringify(v));

// Modello di prova. `sessions`: 0, 1 o 6 (gli altri numeri prendono le prime n delle sei).
export function testModel({ sessions = 1, lowBatt = false, night = false, perm = false, pcOff = false, frame = 0, now = BASE.now } = {}) {
  const n = Math.max(0, Math.min(6, Number.isFinite(sessions) ? Math.trunc(sessions) : 1));
  const list = clone(n in SESSIONS ? SESSIONS[n] : SESSIONS[6].slice(0, n));
  if (perm) {
    if (!list.length) list.push({ name: 'api-server', state: 'permission', ago: 720000 });
    list[0].state = 'permission';
    list[0].detail = 'Bash: rm -rf build';
  }
  const over = { now, local: BASE.local + (now - BASE.now), frame, sessions: list };
  if (lowBatt) { over.sys = { batt: 9 }; over.bat = { trend: 1.5 }; }
  if (night) over.set = { night: true };
  if (pcOff) over.sys = { ...over.sys, pc: false, pcOff: true };
  return makeModel(BASE, over);
}
