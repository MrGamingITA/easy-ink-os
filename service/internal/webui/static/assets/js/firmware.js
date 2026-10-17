// Firmware via USB con Web Serial + esptool-js 0.7.0 (vendor/): collega, rileva il chip, scrive con
// avanzamento e verifica MD5; backup (completo o solo programma) salvati sul servizio, ripristino.
// Prima di ogni scrittura una modale chiede se fare un backup del firmware attuale.
import { getFirmware, listBackups, uploadBackup, fetchBackupData, deleteBackup, backupDataUrl } from './api.js';
import { h, heroGoccia, fmtBytes, fmtDuration, hex } from './ui.js';
import { md5Hex } from './md5.js';
import { parsePartitionTable, activeApp, PART_TABLE_OFFSET, PART_TABLE_SIZE } from './partitions.js';
import { initOta } from './ota.js';

const ESPTOOL_URL = '../../vendor/esptool-js-0.7.0/esptool.js';
const ROM_BAUD = 115200; // velocità del bootloader in ROM, prima del cambio di baud
const $ = (id) => document.getElementById(id);
const goccia = heroGoccia('idle', 'Nessuna scheda collegata via USB');
let otaUi = null; // sezione «Aggiornamento senza cavo» (ota.js)

const S = {
  port: null, transport: null, loader: null, baud: ROM_BAUD,
  chipName: '', imageChipId: undefined, flashBytes: 0, mac: '',
  app: undefined,      // app attiva: { part, reason } | null se non leggibile | undefined se non ancora letta
  busy: '', cancel: false,
  fileData: null, fileName: '', pkgParts: null, pkg: null,
  backups: [], backupsStatus: 0,
  sticky: null         // { pose, text }: esito dell'ultima operazione, finché non ne parte un'altra
};
const blobUrls = [];
const hasSerial = 'serial' in navigator && window.isSecureContext;
const errText = (e) => (e && e.message ? e.message : String(e));
const normMac = (m) => String(m || '').toLowerCase().replace(/[^0-9a-f]/g, '');
// Nome della scheda come in PROTOCOLLO.md: calamaio-xxxx, ultime 4 cifre esadecimali del MAC.
const boardFromMac = (m) => (normMac(m).length >= 4 ? 'calamaio-' + normMac(m).slice(-4) : '');
const sameChip = (a, b) => String(a || '').replace(/[^a-z0-9]/gi, '').toUpperCase() === String(b || '').replace(/[^a-z0-9]/gi, '').toUpperCase();
const sha256Hex = async (d) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', d))].map((b) => b.toString(16).padStart(2, '0')).join('');
const parseOffset = (v) => (typeof v === 'number' ? v : String(v).toLowerCase().startsWith('0x') ? parseInt(v, 16) : parseInt(v, 10));
// Durata minima di una lettura: 10 bit per byte sulla seriale (8N1). [stimato]
const readSeconds = (bytes) => bytes / (S.baud / 10);

/* ---------- registro ---------- */
const logEl = $('log');
let onTermLine = null;
function log(line) {
  logEl.textContent += line + '\n';
  if (logEl.textContent.length > 200000) logEl.textContent = logEl.textContent.slice(-150000);
  logEl.scrollTop = logEl.scrollHeight;
}
const terminal = {
  clean() {},
  writeLine(d) { log(String(d)); if (onTermLine) onTermLine(String(d)); },
  write(d) { logEl.textContent += String(d); }
};

/* ---------- avanzamento ---------- */
function progress(box) {
  let total = 0, t0 = 0, phase = '';
  const ph = h('div', { class: 'phase' });
  const fill = h('span');
  const bar = h('div', { class: 'bar big accent', role: 'progressbar', 'aria-valuemin': '0', 'aria-valuemax': '100', 'aria-valuenow': '0' }, fill);
  const nums = h('div', { class: 'nums', 'aria-live': 'off' });
  return {
    start(tot, label) { total = tot; t0 = performance.now(); box.hidden = false; box.replaceChildren(ph, bar, nums); this.phase(label); this.update(0); },
    phase(label) { if (label !== phase) { phase = label; ph.textContent = label; bar.setAttribute('aria-label', label); } },
    update(done) {
      const pct = total ? Math.min(100, (100 * done) / total) : 0;
      const sec = (performance.now() - t0) / 1000, speed = sec > 0.5 ? done / sec : 0;
      const left = speed > 0 ? (total - done) / speed : NaN;
      fill.style.width = pct.toFixed(1) + '%';
      bar.setAttribute('aria-valuenow', String(Math.floor(pct)));
      nums.replaceChildren(
        h('span', null, fmtBytes(Math.round(done)) + ' di ' + fmtBytes(total)),
        h('span', null, Math.floor(pct) + '%'),
        h('span', null, speed ? fmtBytes(Math.round(speed)) + '/s' : '— /s'),
        h('span', null, 'mancano ' + (done >= total ? '0 s' : fmtDuration(left))));
    },
    elapsed() { return (performance.now() - t0) / 1000; }
  };
}
const backupProg = progress($('backup-progress'));
const writeProg = progress($('write-progress'));
const restoreProg = progress($('restore-progress'));

/* ---------- stato della pagina ---------- */
function setSticky(pose, text) { S.sticky = { pose, text }; refresh(); }

function refresh() {
  const connected = !!S.loader, busy = !!S.busy;
  $('connect').hidden = connected;
  $('connect').disabled = !hasSerial || busy;
  $('disconnect').hidden = !connected;
  $('disconnect').disabled = busy;
  $('baud').disabled = connected || busy;
  $('backup-full').disabled = !connected || busy || !S.flashBytes;
  $('backup-app').disabled = !connected || busy || S.app === null;
  $('backup-cancel').hidden = !busy || !S.reading;
  const img = currentImage();
  $('write').disabled = !(connected && !busy && img && !img.error && $('confirm').value.trim() === 'SCRIVI');
  document.querySelectorAll('input[name="src"], input[name="addr"], #file, #erase').forEach((el) => { el.disabled = busy; });
  document.querySelectorAll('[data-restore]').forEach((b) => { b.disabled = !connected || busy; });
  document.querySelectorAll('[data-delete]').forEach((b) => { b.disabled = busy; });

  if (busy) return; // durante le operazioni la Goccia la imposta chi lavora
  const og = otaUi && otaUi.goccia(); // aggiornamento senza cavo in corso o appena finito
  if (og) return goccia.set(og.pose, og.text);
  if (S.sticky) return goccia.set(S.sticky.pose, S.sticky.text);
  if (!hasSerial) return goccia.set('idle', 'Web Serial non disponibile in questo browser');
  if (!connected) return goccia.set('idle', 'Nessuna scheda collegata via USB');
  goccia.set('input', 'Scheda collegata: scegli cosa fare');
}

function setBusy(what, text) { S.busy = what; S.sticky = null; goccia.set('working', text); refresh(); }
function clearBusy() { S.busy = ''; S.reading = false; refresh(); }

window.addEventListener('beforeunload', (e) => { if (S.busy) { e.preventDefault(); e.returnValue = ''; } });

/* ---------- modali ---------- */
// Apre un <dialog> e risolve con il value del pulsante premuto ('' se chiuso con Esc).
function ask(dlg) {
  return new Promise((resolve) => {
    dlg.returnValue = '';
    dlg.addEventListener('close', () => resolve(dlg.returnValue), { once: true });
    dlg.showModal();
  });
}

/* ---------- collegamento ---------- */
let esptool = null;
async function loadEsptool() { if (!esptool) esptool = await import(ESPTOOL_URL); return esptool; }

const USB_NAMES = { '10c4:ea60': 'Silicon Labs CP210x', '1a86:7523': 'WCH CH340', '1a86:55d4': 'WCH CH9102', '303a:1001': 'Espressif USB‑JTAG', '0403:6001': 'FTDI FT232' };

async function connect() {
  let port;
  try { port = await navigator.serial.requestPort(); } catch (e) { return; } // scelta annullata
  const baud = parseInt($('baud').value, 10);
  setBusy('connect', 'Collegamento alla scheda…');
  $('chip').hidden = true;
  log('— collegamento a ' + baud + ' baud —');
  try {
    const { ESPLoader, Transport } = await loadEsptool();
    const transport = new Transport(port, false);
    const loader = new ESPLoader({ transport, baudrate: baud, romBaudrate: ROM_BAUD, terminal, debugLogging: false });
    S.port = port; S.transport = transport;
    const desc = await loader.main();
    const chip = loader.chip;
    const safe = async (fn) => { try { return await fn(); } catch (e) { return null; } };
    const rev = (/revision (v[\d.]+)/.exec(desc) || [])[1] ||
      (chip.getChipRevision ? await safe(async () => { const r = await chip.getChipRevision(loader); return 'v' + Math.floor(r / 100) + '.' + (r % 100); }) : null);
    const features = await safe(() => chip.getChipFeatures(loader));
    const crystal = await safe(() => chip.getCrystalFreq(loader));
    const mac = await safe(() => chip.readMac(loader));
    const flashId = await safe(() => loader.readFlashId());
    const sizeStr = await safe(() => loader.detectFlashSize());
    S.loader = loader; S.baud = baud;
    S.chipName = chip.CHIP_NAME;
    S.imageChipId = chip.IMAGE_CHIP_ID;
    S.mac = mac || '';
    S.flashBytes = sizeStr ? loader.flashSizeBytes(sizeStr) : 0;
    S.app = undefined;
    const info = port.getInfo ? port.getInfo() : {};
    const vidpid = info.usbVendorId !== undefined ? info.usbVendorId.toString(16).padStart(4, '0') + ':' + info.usbProductId.toString(16).padStart(4, '0') : '';
    const rows = [
      ['Chip', desc.replace(/\s*\(revision [^)]*\)/, '')],
      ['Revisione', rev || '—'],
      ['Funzioni', features ? features.join(', ') : '—'],
      ['Cristallo', crystal ? crystal + ' MHz' : '—'],
      ['MAC', mac || '—'],
      ['Scheda', boardFromMac(mac) || '—'],
      ['Flash', S.flashBytes ? fmtBytes(S.flashBytes) + (flashId !== null ? ' · id ' + hex(flashId, 6) : '') : 'dimensione non rilevata'],
      ['Porta USB', vidpid ? vidpid + (USB_NAMES[vidpid] ? ' · ' + USB_NAMES[vidpid] : '') : '—'],
      ['Velocità', baud + ' baud']
    ];
    $('chip').replaceChildren(...rows.flatMap(([k, v]) => [h('dt', null, k), h('dd', { class: k === 'MAC' || k === 'Flash' || k === 'Scheda' ? 'mono' : null }, v)]));
    $('chip').hidden = false;
    $('connect-hint').textContent = 'Scheda collegata. Quando hai finito, «Scollega e riavvia» la fa ripartire con il suo firmware.';
    updateEstimate();
    checkImage();
    renderBackups();
    clearBusy();
  } catch (e) {
    log('Errore: ' + errText(e));
    await dropConnection(false);
    clearBusy();
    setSticky('error', 'La scheda non risponde');
    $('connect-hint').textContent = 'Collegamento non riuscito: ' + errText(e) + '. Controlla il cavo (deve portare i dati, non solo la corrente), chiudi altri programmi che usano la porta (monitor seriale, esptool) e riprova; se serve, tieni premuto BOOT/PRG mentre colleghi.';
  }
}

async function dropConnection(reset) {
  const { loader, transport } = S;
  S.loader = null; S.flashBytes = 0; S.chipName = ''; S.imageChipId = undefined; S.app = undefined;
  if (reset && loader) { try { await loader.after('hard_reset'); } catch (e) { /* la scheda può essere già sparita */ } }
  if (transport) { try { await transport.disconnect(); } catch (e) { /* già chiusa */ } }
  S.transport = null; S.port = null;
  $('chip').hidden = true;
  $('backup-estimate').textContent = 'Collega la scheda (passo 1) per fare un backup.';
  renderBackups();
}

async function disconnect() {
  setBusy('disconnect', 'Riavvio della scheda…');
  await dropConnection(true);
  log('— scollegata, riavvio —');
  $('connect-hint').textContent = 'Scheda scollegata e riavviata con il suo firmware.';
  clearBusy();
  S.sticky = null; refresh();
}

if (hasSerial) {
  navigator.serial.addEventListener('disconnect', async (ev) => {
    if (!S.port || ev.target !== S.port) return;
    const was = S.busy;
    log('— porta USB sparita —');
    await dropConnection(false);
    S.busy = '';
    setSticky('error', was ? 'Cavo scollegato durante l’operazione' : 'Scheda scollegata');
  });
}

/* ---------- lettura della flash e backup ---------- */
function updateEstimate() {
  if (!S.flashBytes) { $('backup-estimate').textContent = 'Dimensione della flash non rilevata: backup completo non disponibile.'; return; }
  const app = S.app && S.app.part;
  $('backup-estimate').textContent = 'Backup completo: ' + fmtBytes(S.flashBytes) + ', almeno ' + fmtDuration(readSeconds(S.flashBytes)) +
    ' a ' + S.baud + ' baud (stima: byte ÷ ' + (S.baud / 10) + ' byte/s).' +
    (app ? ' Solo il programma (' + app.label + '): ' + fmtBytes(app.size) + ', almeno ' + fmtDuration(readSeconds(app.size)) + '.' : '') +
    ' Non scollegare il cavo nel frattempo.';
}

async function readRegion(offset, length, prog, label) {
  S.cancel = false; S.reading = true;
  $('backup-cancel').disabled = false;
  refresh();
  if (prog) prog.start(length, label);
  log('— lettura di ' + length + ' byte da ' + hex(offset) + ' —');
  try {
    return await S.loader.readFlash(offset, length, (pkt, got) => {
      if (S.cancel) throw new Error('annullato');
      if (prog) prog.update(got);
    });
  } finally { S.reading = false; refresh(); }
}

// Legge tabella delle partizioni (0x8000) e otadata per sapere quale app avvia la scheda.
async function findActiveApp() {
  if (S.app !== undefined) return S.app;
  try {
    const table = parsePartitionTable(await readRegion(PART_TABLE_OFFSET, PART_TABLE_SIZE, null));
    if (!table) { S.app = null; log('Tabella delle partizioni non trovata a 0x8000.'); return null; }
    const ota = table.find((p) => p.type === 0x01 && p.subtype === 0x00);
    const otadata = ota ? await readRegion(ota.offset, Math.min(ota.size, 0x2000), null) : null;
    S.app = activeApp(table, otadata);
    if (S.app) log('App attiva: ' + S.app.part.label + ' a ' + hex(S.app.part.offset) + ', ' + S.app.part.size + ' byte (' + S.app.reason + ').');
  } catch (e) {
    log('Lettura delle partizioni non riuscita: ' + errText(e));
    S.app = null;
  }
  updateEstimate();
  return S.app;
}

function stamp() {
  const d = new Date(), p2 = (n) => String(n).padStart(2, '0');
  return { file: d.getFullYear() + p2(d.getMonth() + 1) + p2(d.getDate()) + '-' + p2(d.getHours()) + p2(d.getMinutes()),
    human: p2(d.getDate()) + '/' + p2(d.getMonth() + 1) + '/' + d.getFullYear() + ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes()) };
}

function localLink(data, name) {
  const url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
  blobUrls.push(url);
  return h('a', { class: 'btn small secondary', href: url, download: name }, 'Scarica la copia letta (' + fmtBytes(data.length) + ')');
}

// Legge la regione, calcola lo SHA-256 e la carica sul servizio. Restituisce il backup creato.
// Se il caricamento non riesce lancia un errore con .local = link per scaricare la copia letta.
async function makeBackup(kind, prog, note) {
  let offset = 0, length = S.flashBytes, label = 'Backup completo';
  if (kind === 'app') {
    const app = await findActiveApp();
    if (!app) throw new Error('tabella delle partizioni non leggibile: backup del solo programma non disponibile');
    offset = app.part.offset; length = app.part.size; label = 'Solo programma (' + app.part.label + ')';
  }
  const data = await readRegion(offset, length, prog, label + ': lettura');
  prog.phase('Calcolo dello SHA-256');
  const sha = await sha256Hex(data);
  const st = stamp();
  const name = label + ' · ' + st.human;
  const fileName = 'calamaio-' + (kind === 'app' ? 'app' : 'backup') + '-' + S.chipName.toLowerCase() + '-' + normMac(S.mac) + '-' + st.file + '.bin';
  log(name + ': ' + data.length + ' byte, SHA-256 ' + sha);
  prog.phase('Salvataggio sul servizio');
  let r;
  try {
    r = await uploadBackup({ name, chip: S.chipName, mac: S.mac, board: boardFromMac(S.mac), flashSize: S.flashBytes, offset, note }, data);
  } catch (e) { r = { status: 0, body: { error: 'il servizio sul PC non risponde' } }; }
  if (r.status !== 201) {
    const why = r.status === 413 ? 'il servizio lo rifiuta perché è troppo grande (413)'
      : r.status === 404 ? 'questo servizio non gestisce ancora i backup'
      : (r.body && r.body.error) || ('HTTP ' + r.status);
    const err = new Error('backup letto ma non salvato sul servizio: ' + why);
    err.local = localLink(data, fileName);
    throw err;
  }
  prog.phase('Backup salvato');
  await loadBackups();
  return { backup: (r.body && r.body.backup) || null, sha };
}

async function manualBackup(kind) {
  setBusy('backup', kind === 'app' ? 'Backup del programma in corso' : 'Backup completo in corso');
  $('backup-result').replaceChildren();
  try {
    const { backup, sha } = await makeBackup(kind, backupProg, 'backup manuale');
    $('backup-result').replaceChildren(h('p', { class: 'alert ok' }, h('strong', null, 'Backup salvato sul servizio. '),
      (backup ? '«' + backup.name + '», ' : '') + 'SHA-256 ' + sha.slice(0, 16) + '…'));
    clearBusy();
    setSticky('done', 'Backup completato');
  } catch (e) {
    await failRead(e, backupProg, $('backup-result'), 'Backup');
  }
}

// Errore durante una lettura: se la seriale è in uno stato incerto si riparte puliti.
async function failRead(e, prog, box, what) {
  const cancelled = S.cancel;
  log(what + ' interrotto: ' + errText(e));
  prog.phase(cancelled ? what + ' annullato' : what + ' non riuscito');
  const kids = [h('p', { class: 'alert error' }, cancelled ? what + ' annullato. La scheda è stata scollegata: ricollegala per continuare.'
    : what + ' non riuscito: ' + errText(e) + (e.local ? '. Scarica la copia qui sotto e tienila da parte.' : '. Se usavi più di 115200 baud, riprova a 115200.'))];
  if (e.local) kids.push(h('div', { class: 'card-actions' }, e.local));
  box.replaceChildren(...kids);
  if (!e.local) await dropConnection(true); // dopo una lettura interrotta lo stub continua a mandare dati
  clearBusy();
  setSticky(cancelled ? 'idle' : 'error', cancelled ? what + ' annullato' : what + ' non riuscito');
}

/* ---------- modale «Vuoi fare un backup?» ---------- */
async function askBackup(context) {
  setBusy('ask', 'Leggo la tabella delle partizioni');
  await findActiveApp();
  clearBusy();
  if (!S.loader) return 'cancel';
  $('pb-context').textContent = context;
  $('pb-full').disabled = !S.flashBytes;
  $('pb-full-text').textContent = S.flashBytes
    ? 'Tutta la flash (' + fmtBytes(S.flashBytes) + ': bootloader, partizioni, programma e impostazioni). Circa ' + fmtDuration(readSeconds(S.flashBytes)) + ' a ' + S.baud + ' baud.'
    : 'Dimensione della flash non rilevata.';
  const app = S.app && S.app.part;
  $('pb-app').disabled = !app;
  $('pb-app-text').textContent = app
    ? 'La partizione ' + app.label + ' a ' + hex(app.offset) + ' (' + fmtBytes(app.size) + '), quella che la scheda avvia adesso (' + S.app.reason + '). Circa ' + fmtDuration(readSeconds(app.size)) + '.'
    : 'Non disponibile: la tabella delle partizioni a 0x8000 non si legge.';
  const v = await ask($('dlg-prebackup'));
  return v || 'cancel';
}

/* ---------- immagine da scrivere ---------- */
function srcMode() { return document.querySelector('input[name="src"]:checked').value; }
function addrValue() { return parseInt(document.querySelector('input[name="addr"]:checked').value, 10); }
const PART_NAMES = { 0: 'bootloader', 4096: 'bootloader', 32768: 'partizioni', 57344: 'boot_app0', 65536: 'app' };
const hasPartTable = (d) => d.length > 0x8002 && d[0x8000] === 0xAA && d[0x8001] === 0x50;

// { parts: [{address, data, name}], total, error? } oppure null se manca il firmware.
function currentImage() {
  if (srcMode() === 'file') {
    if (!S.fileData) return null;
    const address = addrValue(), d = S.fileData;
    const img = { parts: [{ address, data: d, name: S.fileName }], total: d.length };
    // Intestazione ESP (0xE9): nell'unico a 0x0 per ESP32 classico il bootloader sta a 0x1000.
    const hdr = address === 0 && d[0] !== 0xE9 && d[0x1000] === 0xE9 ? 0x1000 : 0;
    if (d[hdr] !== 0xE9) img.error = 'Il file non sembra un firmware ESP32 (manca l’intestazione 0xE9 all’inizio).';
    else if (S.imageChipId !== undefined && d.length > hdr + 13) {
      const id = d[hdr + 12] | (d[hdr + 13] << 8);
      if (id !== S.imageChipId) img.error = 'Il file è per un altro chip (id immagine ' + id + ', la scheda collegata è ' + S.chipName + ').';
    }
    if (!img.error && S.flashBytes && address + d.length > S.flashBytes) img.error = 'Il file non entra nella flash (' + fmtBytes(S.flashBytes) + ') a partire da ' + hex(address) + '.';
    return img;
  }
  if (!S.pkgParts) return null;
  const img = { parts: S.pkgParts, total: S.pkgParts.reduce((a, p) => a + p.data.length, 0) };
  const want = (S.pkg && S.pkg.chip) || '';
  if (S.chipName && want && !sameChip(want, S.chipName)) img.error = 'Il pacchetto è per ' + want + ', ma la scheda collegata è ' + S.chipName + '.';
  return img;
}

function checkImage() {
  const info = $('file-info');
  if (srcMode() === 'file') {
    if (!S.fileData) { info.textContent = 'Scegli il file .bin.'; info.className = 'hint'; }
    else {
      const d = S.fileData, merged = hasPartTable(d), a = addrValue();
      let txt = S.fileName + ' · ' + fmtBytes(d.length) + '. ';
      txt += merged ? 'Contiene una tabella delle partizioni a 0x8000: è un’immagine unica, va scritta a 0x0.'
        : 'Non contiene la tabella delle partizioni: di solito è solo l’app, da scrivere a 0x10000.';
      if ((merged && a !== 0) || (!merged && a === 0)) txt += ' Attenzione: l’indirizzo scelto non è quello suggerito.';
      const img = currentImage();
      info.textContent = txt + (img && img.error ? ' ' + img.error : '');
      info.className = 'hint' + (img && img.error ? ' alert error' : '');
    }
  }
  if (srcMode() === 'pkg' && S.pkgParts) {
    const img = currentImage();
    $('pkg-info').className = img && img.error ? 'alert error' : 'hint';
    if (img && img.error) $('pkg-info').textContent = img.error;
  }
  refresh();
}

async function onFile() {
  const f = $('file').files[0];
  S.fileData = null; S.fileName = '';
  if (f) {
    S.fileData = new Uint8Array(await f.arrayBuffer());
    S.fileName = f.name;
    const a = hasPartTable(S.fileData) ? '0' : '65536'; // indirizzo suggerito dal contenuto
    document.querySelector('input[name="addr"][value="' + a + '"]').checked = true;
  }
  checkImage();
}

async function loadPackage() {
  const info = $('pkg-info'), box = $('pkg-parts');
  if (S.pkg) return;
  info.className = 'hint'; info.textContent = 'Lettura del pacchetto dal servizio…';
  let man;
  try { man = await getFirmware(); } catch (e) { info.className = 'alert error'; info.textContent = 'Il servizio sul PC non risponde: pacchetto non disponibile.'; return; }
  if (!man || !man.available || !Array.isArray(man.parts) || !man.parts.length) {
    info.className = 'alert info';
    info.textContent = 'Il servizio non offre un firmware adesso. Usa un file dal computer.';
    return;
  }
  S.pkg = man;
  const rows = man.parts.map((p) => ({ p, offset: parseOffset(p.offset), state: h('td', null, 'da scaricare') }));
  rows.sort((a, b) => a.offset - b.offset);
  box.replaceChildren(h('table', { class: 'parts' },
    h('thead', null, h('tr', null, ...['Indirizzo', 'Parte', 'Dimensione', 'SHA-256', 'Controllo'].map((t) => h('th', { scope: 'col' }, t)))),
    h('tbody', null, ...rows.map((r) => h('tr', null,
      h('td', null, hex(r.offset, 4)), h('td', null, r.p.name || PART_NAMES[r.offset] || 'parte'),
      h('td', null, fmtBytes(r.p.size)), h('td', { title: r.p.sha256 }, (r.p.sha256 || '').slice(0, 12) + '…'), r.state)))));
  info.textContent = 'Firmware ' + (man.version || '?') + ' per ' + (man.chip || '?') + ' · ' + rows.length + ' parti. Scaricamento e controllo…';
  const parts = [];
  try {
    for (const r of rows) {
      r.state.textContent = 'scarico…';
      const res = await fetch(r.p.url, { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status + ' per ' + r.p.url);
      const data = new Uint8Array(await res.arrayBuffer());
      if (r.p.size !== undefined && data.length !== r.p.size) throw new Error('dimensione diversa dal manifest (' + data.length + ' invece di ' + r.p.size + ')');
      const sha = await sha256Hex(data);
      if (r.p.sha256 && sha !== String(r.p.sha256).toLowerCase()) throw new Error('SHA-256 diverso dal manifest per ' + (r.p.name || hex(r.offset)));
      r.state.textContent = '✓ ok';
      parts.push({ address: r.offset, data, name: r.p.name || PART_NAMES[r.offset] || hex(r.offset) });
    }
    S.pkgParts = parts;
    info.className = 'hint';
    info.textContent = 'Firmware ' + (man.version || '?') + ' per ' + (man.chip || '?') + ': ' + parts.length + ' parti scaricate, dimensioni e SHA-256 corrispondono al manifest.';
  } catch (e) {
    S.pkgParts = null; S.pkg = null;
    info.className = 'alert error';
    info.textContent = 'Pacchetto non valido: ' + e.message + '. Non verrà scritto.';
  }
  checkImage();
}

function onSrc() {
  const pkg = srcMode() === 'pkg';
  $('src-file').hidden = pkg;
  $('src-pkg').hidden = !pkg;
  if (pkg) loadPackage();
  checkImage();
}

/* ---------- scrittura (firmware nuovo o ripristino) ---------- */
// Prima la modale del backup, poi il backup (se scelto), poi la scrittura con verifica MD5.
async function backupThenWrite(parts, opt) {
  const choice = await askBackup(opt.context);
  if (choice === 'cancel' || !S.loader) { log('Scrittura annullata prima di iniziare.'); return false; }
  const { prog, box, what } = opt;
  box.replaceChildren();
  if (choice === 'full' || choice === 'app') {
    setBusy(opt.busy, 'Backup prima della scrittura');
    try { await makeBackup(choice, prog, opt.note); }
    catch (e) {
      await failRead(e, prog, box, 'Backup');
      if (e.local) box.prepend(h('p', { class: 'alert error' }, h('strong', null, 'La scrittura non è partita. '), 'Salva la copia letta; poi, se vuoi, riprova scegliendo «Nessun backup».'));
      return false;
    }
  }
  return writeParts(parts, opt, choice !== 'none');
}

async function writeParts(parts, opt, backedUp) {
  const { prog, box, erase } = opt;
  const total = parts.reduce((a, p) => a + p.data.length, 0);
  setBusy(opt.busy, opt.what + ' in corso');
  const sizes = parts.map((p) => p.data.length);
  const before = (i) => sizes.slice(0, i).reduce((a, b) => a + b, 0);
  let verified = 0;
  onTermLine = (d) => { if (d.includes('Hash of data verified')) verified++; };
  prog.start(total, erase ? 'Cancellazione della flash…' : 'Scrittura');
  log('— scrittura: ' + parts.map((p) => p.name + ' @ ' + hex(p.address)).join(', ') + (erase ? ', con cancellazione completa' : '') + ' —');
  try {
    await S.loader.writeFlash({
      fileArray: parts.map((p) => ({ data: p.data, address: p.address })),
      flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: !!erase, compress: true,
      reportProgress: (i, written, tot) => {
        prog.update(before(i) + (tot ? written / tot : 0) * sizes[i]);
        const p = parts[i];
        prog.phase(written >= tot && tot > 0 ? 'Verifica di ' + p.name + ' (MD5)' : 'Scrittura di ' + p.name + ' a ' + hex(p.address) + ' (' + (i + 1) + '/' + parts.length + ')');
      },
      calculateMD5Hash: (image) => md5Hex(image)
    });
    const sec = prog.elapsed();
    onTermLine = null;
    prog.phase('Riavvio della scheda');
    await dropConnection(true);
    prog.phase(opt.what + ' completato e verificato');
    box.replaceChildren(h('p', { class: 'alert ok' }, h('strong', null, opt.what + ' completato e verificato. '),
      fmtBytes(total) + ' in ' + fmtDuration(sec) + '; ' + verified + ' su ' + parts.length + ' parti con MD5 uguale a quello del file.' +
      (backedUp ? ' Il backup fatto prima è nell’elenco dei backup.' : '') + ' La scheda si riavvia.'));
    $('connect-hint').textContent = 'La scheda si è riavviata. Per un’altra operazione, collegala di nuovo.';
    clearBusy();
    setSticky('done', opt.what + ' completato');
    return true;
  } catch (e) {
    onTermLine = null;
    log(opt.what + ' non riuscito: ' + errText(e));
    prog.phase(opt.what + ' non riuscito');
    box.replaceChildren(h('p', { class: 'alert error' }, h('strong', null, opt.what + ' non riuscito: '),
      errText(e) + '. La scheda può non avviarsi finché non scrivi un firmware completo: ricollegala e riprova (a 115200 baud), oppure ripristina un backup completo.'));
    await dropConnection(false);
    clearBusy();
    setSticky('error', opt.what + ' non riuscito');
    return false;
  }
}

async function write() {
  const img = currentImage();
  if (!img || img.error || $('confirm').value.trim() !== 'SCRIVI') return;
  const src = srcMode() === 'pkg' ? 'il firmware ' + ((S.pkg && S.pkg.version) || '') + ' del servizio' : 'il file ' + S.fileName;
  const ok = await backupThenWrite(img.parts, {
    prog: writeProg, box: $('write-result'), busy: 'write', what: 'Scrittura', erase: $('erase').checked,
    context: 'Stai per scrivere ' + src + ' (' + fmtBytes(img.total) + ') sulla scheda ' + (boardFromMac(S.mac) || S.chipName) + '.',
    note: 'prima di scrivere ' + src
  });
  if (ok) $('confirm').value = '';
  refresh();
}

/* ---------- elenco dei backup ---------- */
const isFull = (b) => Number(b.offset) === 0 && b.flashSize && Number(b.length) === Number(b.flashSize);
function regionText(b) {
  const off = Number(b.offset) || 0, len = Number(b.length || b.size) || 0;
  return (isFull(b) ? 'completo' : off === 0 ? 'parziale' : 'solo programma') + ' · ' + hex(off) + '–' + hex(off + len) + ' (' + fmtBytes(len) + ')';
}
function dateText(v) {
  if (!v) return '—';
  const d = new Date(v);
  return isNaN(d) ? String(v) : d.toLocaleString('it-IT', { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });
}
const isFactory = (b) => b.kind === 'factory';

async function loadBackups() {
  try {
    const r = await listBackups();
    S.backupsStatus = r.status; S.backups = r.backups;
  } catch (e) { S.backupsStatus = 0; S.backups = []; }
  renderBackups();
}

function renderBackups() {
  const list = $('backup-list');
  const n = S.backups.length;
  $('backups-count').textContent = S.backupsStatus === 200 ? (n === 1 ? '1 salvato sul PC' : n + ' salvati sul PC')
    : S.backupsStatus === 404 ? 'non disponibili su questo servizio' : 'servizio non raggiungibile';
  if (S.backupsStatus !== 200) {
    list.replaceChildren(h('div', { class: 'empty' }, h('strong', null, 'Elenco dei backup non disponibile'),
      S.backupsStatus === 404 ? 'Il servizio sul PC non gestisce ancora i backup. Puoi comunque leggerne uno: lo scarichi come file.' : 'Il servizio sul PC non risponde.'));
    return;
  }
  const sorted = [...S.backups].sort((a, b) => (isFactory(b) - isFactory(a)) || (!!b.protected - !!a.protected) || (new Date(b.created) - new Date(a.created)));
  if (!sorted.length) { list.replaceChildren(h('div', { class: 'empty' }, h('strong', null, 'Nessun backup'), 'Fanne uno con i pulsanti qui sopra: compare qui.')); return; }
  const connected = !!S.loader, busy = !!S.busy;
  list.replaceChildren(...sorted.map((b) => {
    const other = connected && b.mac && normMac(b.mac) !== normMac(S.mac);
    const badges = [];
    if (isFactory(b)) badges.push(h('span', { class: 'badge attn' }, 'ORIGINALE DI FABBRICA'));
    if (b.protected) badges.push(h('span', { class: 'badge' }, 'PROTETTO'));
    if (isFull(b)) badges.push(h('span', { class: 'badge soft' }, 'COMPLETO'));
    if (other) badges.push(h('span', { class: 'badge soft' }, 'ALTRA SCHEDA'));
    const fileName = (b.name || b.id).replace(/[^\w.-]+/g, '-').replace(/-+/g, '-') + '.bin';
    return h('article', { class: 'backup' + (isFactory(b) ? ' factory' : ''), 'aria-label': b.name },
      h('div', { class: 'card-head' }, h('span', { class: 't' }, b.name || b.id)),
      badges.length ? h('div', { class: 'badges' }, badges) : null,
      h('dl', { class: 'kv' },
        h('dt', null, 'Data'), h('dd', null, dateText(b.created)),
        h('dt', null, 'Scheda'), h('dd', { class: 'mono' }, [b.board, b.mac].filter(Boolean).join(' · ') || '—'),
        h('dt', null, 'Chip'), h('dd', null, b.chip || '—'),
        h('dt', null, 'Dimensione'), h('dd', null, fmtBytes(Number(b.size || b.length) || 0)),
        h('dt', null, 'Regione'), h('dd', { class: 'mono' }, regionText(b)),
        h('dt', null, 'SHA-256'), h('dd', { class: 'mono', title: b.sha256 || '' }, b.sha256 ? b.sha256.slice(0, 16) + '…' : '—'),
        h('dt', null, 'Nota'), h('dd', null, b.note || '—')),
      isFactory(b) ? h('p', { class: 'hint' }, 'Protetto: non si può eliminare e si può sempre ripristinare.') : null,
      h('div', { class: 'card-actions' },
        h('button', { type: 'button', class: 'btn small danger', 'data-restore': b.id, disabled: !connected || busy, onclick: () => restore(b) }, 'Ripristina'),
        h('a', { class: 'btn small secondary', href: backupDataUrl(b.id), download: fileName }, 'Scarica'),
        b.protected || isFactory(b) ? null : h('button', { type: 'button', class: 'btn small secondary', 'data-delete': b.id, disabled: busy, onclick: () => remove(b) }, 'Elimina')),
      connected ? null : h('p', { class: 'hint' }, 'Per ripristinare, collega la scheda (passo 1).'));
  }));
}

async function remove(b) {
  $('dl-text').textContent = 'Il backup «' + (b.name || b.id) + '» (' + fmtBytes(Number(b.size || b.length) || 0) + ') verrà cancellato dal PC. Non si annulla.';
  if (await ask($('dlg-delete')) !== 'delete') return;
  $('backups-err').textContent = '';
  let r;
  try { r = await deleteBackup(b.id); } catch (e) { $('backups-err').textContent = 'Il servizio sul PC non risponde: backup non eliminato.'; return; }
  if (r.status === 204 || r.status === 200) log('Backup eliminato: ' + b.name);
  else if (r.status === 403) $('backups-err').textContent = 'Il backup «' + b.name + '» è protetto: il servizio non lo elimina.';
  else if (r.status === 404) $('backups-err').textContent = 'Il backup «' + b.name + '» non c’era più.';
  else $('backups-err').textContent = 'Eliminazione non riuscita (HTTP ' + r.status + (r.body && r.body.error ? ': ' + r.body.error : '') + ').';
  await loadBackups();
}

async function restore(b) {
  if (!S.loader || S.busy) return;
  const off = Number(b.offset) || 0, len = Number(b.length || b.size) || 0;
  const other = !b.mac || normMac(b.mac) !== normMac(S.mac);
  let block = '';
  if (b.chip && !sameChip(b.chip, S.chipName)) block = 'Il backup è di un ' + b.chip + ', la scheda collegata è un ' + S.chipName + ': non si può ripristinare.';
  else if (S.flashBytes && off + len > S.flashBytes) block = 'Il backup (' + fmtBytes(len) + ' da ' + hex(off) + ') non entra nella flash della scheda collegata (' + fmtBytes(S.flashBytes) + ').';

  $('rs-title').textContent = 'Ripristina «' + (b.name || b.id) + '»';
  $('rs-info').replaceChildren(
    h('dt', null, 'Regione'), h('dd', { class: 'mono' }, regionText(b)),
    h('dt', null, 'Dal backup'), h('dd', { class: 'mono' }, [b.board, b.mac].filter(Boolean).join(' · ') || 'scheda non indicata'),
    h('dt', null, 'Sulla scheda'), h('dd', { class: 'mono' }, [boardFromMac(S.mac), S.mac].filter(Boolean).join(' · ')));
  $('rs-block').hidden = !block; $('rs-block').textContent = block;
  $('rs-other').hidden = !other || !!block;
  $('rs-other-text').textContent = !b.mac
    ? 'Il backup non dice da quale scheda viene. Se è di un’altra scheda, ' + (isFull(b) ? 'porta con sé la sua identità e le sue impostazioni.' : 'il programma potrebbe non essere adatto a questa.')
    : isFull(b)
      ? 'Il backup è della scheda ' + b.mac + ', quella collegata è ' + S.mac + '. Un backup completo porta con sé l’identità e le impostazioni dell’altra scheda: rete Wi‑Fi e password, chiave di abbinamento con il servizio, impostazioni, calibrazioni. Questa scheda si presenterebbe con i dati dell’altra. Il MAC vero resta quello del chip.'
      : 'Il backup è della scheda ' + b.mac + ', quella collegata è ' + S.mac + '. È solo il programma: non porta impostazioni, ma potrebbe essere un firmware per un’altra scheda o un’altra versione.';
  $('rs-force').value = ''; $('rs-confirm').value = '';
  const valid = () => !block && $('rs-confirm').value.trim() === 'SCRIVI' && (!other || $('rs-force').value.trim() === 'FORZA');
  const upd = () => { $('rs-ok').disabled = !valid(); };
  $('rs-force').oninput = upd; $('rs-confirm').oninput = upd; upd();
  if (await ask($('dlg-restore')) !== 'ok' || !valid()) return;

  const box = $('restore-result');
  box.replaceChildren();
  setBusy('restore', 'Scarico il backup dal servizio');
  let data;
  try {
    data = await fetchBackupData(b.id);
    if (len && data.length !== len) throw new Error('dimensione diversa (' + data.length + ' invece di ' + len + ')');
    if (b.sha256 && (await sha256Hex(data)) !== String(b.sha256).toLowerCase()) throw new Error('SHA-256 diverso da quello registrato');
  } catch (e) {
    box.replaceChildren(h('p', { class: 'alert error' }, 'Backup non utilizzabile: ' + errText(e) + '. Niente è stato scritto.'));
    clearBusy(); setSticky('error', 'Backup non utilizzabile');
    return;
  }
  clearBusy();
  log('Backup «' + b.name + '» scaricato e controllato (' + data.length + ' byte).');
  await backupThenWrite([{ address: off, data, name: b.name || b.id }], {
    prog: restoreProg, box, busy: 'restore', what: 'Ripristino', erase: false,
    context: 'Stai per ripristinare «' + (b.name || b.id) + '» (' + fmtBytes(data.length) + ' a ' + hex(off) + ') sulla scheda ' + (boardFromMac(S.mac) || S.chipName) + '.',
    note: 'prima del ripristino di «' + (b.name || b.id) + '»'
  });
}

/* ---------- avvio ---------- */
if (!hasSerial) {
  $('noserial').hidden = false;
  $('here').textContent = location.origin + location.pathname;
}
$('connect').addEventListener('click', connect);
$('disconnect').addEventListener('click', disconnect);
$('backup-full').addEventListener('click', () => manualBackup('full'));
$('backup-app').addEventListener('click', () => manualBackup('app'));
$('backup-cancel').addEventListener('click', () => { S.cancel = true; $('backup-cancel').disabled = true; });
$('file').addEventListener('change', onFile);
$('write').addEventListener('click', write);
$('confirm').addEventListener('input', refresh);
document.querySelectorAll('input[name="src"]').forEach((el) => el.addEventListener('change', onSrc));
document.querySelectorAll('input[name="addr"]').forEach((el) => el.addEventListener('change', checkImage));
window.addEventListener('pagehide', () => blobUrls.forEach((u) => URL.revokeObjectURL(u)));
otaUi = initOta(() => refresh());
checkImage();
loadBackups();
