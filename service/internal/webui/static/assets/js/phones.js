// Telefoni abbinati (app iPhone): testi della sezione in Dispositivo, senza DOM (provati in
// tools/webui-dev/test-units.mjs).

const pad2 = (n) => String(n).padStart(2, '0');

// Tempo che resta alla finestra di abbinamento, «m:ss», per eccesso.
export function countdown(expires, now) {
  const s = Math.max(0, Math.ceil((expires - now) / 1000));
  return Math.floor(s / 60) + ':' + pad2(s % 60);
}

export function lastUsedText(p, now) {
  if (!p.lastUsed) return 'mai usato';
  const m = Math.max(0, Math.round((now - p.lastUsed) / 60000));
  if (m < 1) return 'usato adesso';
  if (m < 60) return 'usato ' + m + ' min fa';
  if (m < 48 * 60) return 'usato ' + Math.floor(m / 60) + ' h fa';
  return 'usato ' + Math.floor(m / 1440) + ' giorni fa';
}

export function pairedText(p) {
  const d = new Date(p.paired);
  return 'abbinato il ' + pad2(d.getDate()) + '/' + pad2(d.getMonth() + 1) + '/' + d.getFullYear();
}

export function appStateText(app) {
  if (app.error) return 'Spenta: ' + app.error;
  if (app.listening) return 'Accesa sulla porta ' + app.port + ' (HTTPS, impronta ' + String(app.fingerprint).slice(0, 16) + '…)';
  return 'Spenta: si accende quando abbini un telefono';
}
