// Prove senza browser: la Goccia del modulo è identica a mascot() del simulatore, l'MD5 è giusto.
//   node test-units.mjs
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { mascot, MASCOT_W, MASCOT_H } from '../../service/internal/webui/static/assets/js/mascot.js';
import { md5Hex } from '../../service/internal/webui/static/assets/js/md5.js';
import { parsePartitionTable, activeApp, otaEntryValid } from '../../service/internal/webui/static/assets/js/partitions.js';
import { _makeFlash } from './fake-esptool.mjs';
import zlib from 'node:zlib';
import { countdown, lastUsedText, pairedText, appStateText } from '../../service/internal/webui/static/assets/js/phones.js';

const here = path.dirname(fileURLToPath(import.meta.url));
let fail = 0;
const check = (ok, msg) => { if (!ok) { fail++; console.log('FAIL', msg); } };

// 1. mascot(): stessa sequenza di fillRect della funzione originale, per 9 pose × 4 fotogrammi × 3 scale.
const src = fs.readFileSync(path.resolve(here, '../../reference/calamaio-os-browser/assets/calamaio-test.js'), 'utf8');
const a = src.indexOf('function mascot(c, ox'), b = src.indexOf('function drawQR', a);
const HW = /const HW = (\[[^\]]+\]);/.exec(src)[1];
const ref = new Function('INK', 'PAPER', 'const HW = ' + HW + ';\n' + src.slice(a, b) + '\nreturn mascot;')('#000', '#fff');
const rec = () => { const calls = []; return { calls, fillStyle: '', fillRect(x, y, w, h) { calls.push([this.fillStyle, x, y, w, h].join(',')); } }; };
const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
let cases = 0, minX = 99, minY = 99, maxX = -1, maxY = -1;
for (const st of POSES) for (let f = 0; f < 4; f++) for (const s of [1, 2, 5]) for (const cols of [[], ['#1E2022', '#D6D7CF']]) {
  const r1 = rec(), r2 = rec();
  ref(r1, 3, 4, s, st, f, ...cols); mascot(r2, 3, 4, s, st, f, ...cols);
  check(r1.calls.join('|') === r2.calls.join('|'), 'mascot ' + st + ' f' + f + ' s' + s);
  if (s === 1) for (const c of r2.calls) { const [, x, y] = c.split(',').map(Number); minX = Math.min(minX, x - 3); maxX = Math.max(maxX, x - 3); minY = Math.min(minY, y - 4); maxY = Math.max(maxY, y - 4); }
  cases++;
}
console.log('mascot: ' + cases + ' casi confrontati; ingombro x ' + minX + '..' + maxX + ', y ' + minY + '..' + maxY);
check(minX >= 0 && minY >= 0 && maxX < MASCOT_W && maxY < MASCOT_H, 'ingombro dichiarato ' + MASCOT_W + '×' + MASCOT_H);

// 2. MD5 contro node:crypto.
for (const n of [0, 1, 55, 56, 63, 64, 65, 1000, 4096, 100003, 3 * 1048576 + 7]) {
  const buf = crypto.randomBytes(n);
  check(md5Hex(new Uint8Array(buf)) === crypto.createHash('md5').update(buf).digest('hex'), 'md5 n=' + n);
}
console.log('md5: 11 lunghezze confrontate');

// 3. Tabella delle partizioni e otadata (flash finta di fake-esptool.mjs).
const fl = _makeFlash();
const table = parsePartitionTable(fl.subarray(0x8000, 0x8C00));
check(table && table.length === 5 && table[2].label === 'app0' && table[3].offset === 0x340000, 'tabella partizioni');
const ota = fl.slice(0xe000, 0x10000);
check(activeApp(table, ota).part.label === 'app1', 'otadata seq 2 → app1');
check(activeApp(table, new Uint8Array(0x2000).fill(0xff)).part.label === 'app0', 'otadata vuota → app0');
const ota1 = ota.slice(); ota1.fill(0xff, 0x1000, 0x2000);
check(activeApp(table, ota1).part.label === 'app0', 'solo seq 1 → app0');
const otaBad = ota.slice(); otaBad[0x1000 + 28] ^= 0xff;
check(activeApp(table, otaBad).part.label === 'app0', 'crc sbagliato nel secondo settore → vale il primo (app0)');
check(activeApp(table.filter((p) => p.label !== 'app1' && p.label !== 'app0').concat([{ type: 0, subtype: 0, offset: 0x10000, size: 0x100000, label: 'factory' }]), null).part.label === 'factory', 'solo factory');
check(parsePartitionTable(new Uint8Array(0xC00).fill(0xff)) === null, 'tabella vuota → null');
// Il CRC di otadata coincide con binascii.crc32(seq, 0xFFFFFFFF) di otatool.py (zlib.crc32 con valore iniziale).
for (const seq of [1, 2, 7, 123456]) {
  const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, seq, true);
  check(otaEntryValid(b, zlib.crc32(b, 0xFFFFFFFF)), 'crc otadata seq ' + seq);
}
console.log('partizioni: 11 prove');
// Telefoni: testi della sezione in Dispositivo.
{
  const now = Date.UTC(2026, 9, 16, 10, 0, 0);
  check(countdown(now + 300000, now) === '5:00', 'countdown 5:00');
  check(countdown(now + 299001, now) === '5:00', 'countdown arrotonda per eccesso');
  check(countdown(now + 61000, now) === '1:01', 'countdown 1:01');
  check(countdown(now - 5000, now) === '0:00', 'countdown scaduto');
  check(lastUsedText({ lastUsed: 0 }, now) === 'mai usato', 'mai usato');
  check(lastUsedText({ lastUsed: now - 20000 }, now) === 'usato adesso', 'usato adesso');
  check(lastUsedText({ lastUsed: now - 5 * 60000 }, now) === 'usato 5 min fa', 'usato 5 min fa');
  check(lastUsedText({ lastUsed: now - 3 * 3600000 }, now) === 'usato 3 h fa', 'usato 3 h fa');
  check(lastUsedText({ lastUsed: now - 3 * 86400000 }, now) === 'usato 3 giorni fa', 'usato 3 giorni fa');
  check(/^abbinato il \d\d\/\d\d\/2026$/.test(pairedText({ paired: now })), 'abbinato il gg/mm/aaaa');
  check(appStateText({ listening: true, port: 8767, fingerprint: 'ab'.repeat(32) }) === 'Accesa sulla porta 8767 (HTTPS, impronta abababababababab…)', 'porta accesa');
  check(appStateText({ listening: false, port: 8767, fingerprint: '' }) === 'Spenta: si accende quando abbini un telefono', 'porta spenta');
  check(appStateText({ listening: false, error: 'porta occupata' }) === 'Spenta: porta occupata', 'porta con errore');
  console.log('telefoni: 13 testi controllati');
}
console.log(fail ? fail + ' prove fallite' : 'tutto ok');
process.exit(fail ? 1 : 0);
