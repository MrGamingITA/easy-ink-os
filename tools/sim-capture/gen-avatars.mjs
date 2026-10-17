#!/usr/bin/env node
// Libreria degli avatar (avatars/*.avatar.json) → dati per il simulatore e per il firmware.
// Specifica: docs/AVATAR.md (§8 formato CAVT v1). Il pacchetto CAVT lo fa tools/avatar/lib/pack.mjs,
// cioè lo stesso codice della riga di comando `avatar.mjs pack`: qui non c'è un secondo impacchettatore.
//
//   reference/calamaio-os-browser/assets/avatars.js   window.CALAMAIO_AVATARS (CAVT in base64): il
//                                                     simulatore resta apribile con doppio clic
//   firmware/src/gfx/avatar_builtin.cpp               gli stessi byte compilati nel firmware
//
// Gioco della mascotte (docs/AVATAR.md §13–14): nello stesso avatars.js anche window.CALAMAIO_ANCHORS (blocchi
// CANC v1 dei predefiniti, da avatars/ancore/<id>.anchors.json, legati al CRC del CAVT) e window.CALAMAIO_ACCESSORIES
// (CACC v1 del catalogo avatars/accessori/); nel firmware firmware/src/gfx/accessory_builtin.cpp. Pacchetti di
// tools/avatar/lib/anchors.mjs e lib/accessory.mjs, come per gli avatar.
//
// Uso: node tools/sim-capture/gen-avatars.mjs            rigenera i due file
//      node tools/sim-capture/gen-avatars.mjs --check    esce con 1 se i file non sono aggiornati
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { load, validate } from '../avatar/lib/format.mjs';
import { pack, unpack } from '../avatar/lib/pack.mjs';
import { loadWithAnchors, packAnchors, unpackAnchors } from '../avatar/lib/anchors.mjs';
import { validateAccessory, packAccessory, unpackAccessory, canonicalAccessory } from '../avatar/lib/accessory.mjs';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const LIB = path.join(REPO, 'avatars');
const OUT_JS = path.join(REPO, 'reference', 'calamaio-os-browser', 'assets', 'avatars.js');
const OUT_CPP = path.join(REPO, 'firmware', 'src', 'gfx', 'avatar_builtin.cpp');
const OUT_ACC_CPP = path.join(REPO, 'firmware', 'src', 'gfx', 'accessory_builtin.cpp');
const ACC_LIB = path.join(LIB, 'accessori');
// Ordine del catalogo iniziale (specifica del gioco §1.5): è anche l'ordine del pulsante Simula «Gioco: indossa» e
// dello sblocco simulato ai livelli pari, in simulatore e firmware. Gli altri, in ordine alfabetico.
const ACC_ORDER = ['cappellino', 'occhiali', 'sciarpa', 'papillon', 'corona', 'cuffie', 'fiocco', 'mago'];
// Ordine fisso (docs/AVATAR.md §1): è anche l'ordine dei pulsanti Simula «Avatar della sessione» e
// «Avatar predefinito», quindi lo devono avere uguale simulatore e firmware. Gli altri, in ordine alfabetico.
const ORDER = ['goccia', 'gatto', 'demone', 'calamaro'];

function library() {
  const files = fs.readdirSync(LIB).filter((f) => f.endsWith('.avatar.json'));
  const ids = files.map((f) => f.slice(0, -'.avatar.json'.length));
  ids.sort((a, b) => {
    const ia = ORDER.indexOf(a), ib = ORDER.indexOf(b);
    if (ia >= 0 || ib >= 0) return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib);
    return a < b ? -1 : a > b ? 1 : 0;
  });
  if (ids[0] !== 'goccia') throw new Error('manca avatars/goccia.avatar.json: è il ripiego di tutto');
  return ids.map((id) => {
    const file = path.join(LIB, id + '.avatar.json');
    const { av, bytes } = load(file);
    const r = validate(av, { bytes });
    if (r.errors.length) throw new Error(id + ': ' + r.errors.join('; '));
    if (av.id !== id) throw new Error(id + ': id diverso dal nome del file');
    const bin = pack(av);
    const back = unpack(bin);  // andata e ritorno, come `avatar.mjs pack`
    for (const p of Object.keys(av.poses)) {
      if (JSON.stringify(av.poses[p]) !== JSON.stringify(back.poses[p])) throw new Error(id + ': andata e ritorno CAVT diversa in ' + p);
    }
    const crc = bin.readUInt32LE(8);
    // Ancore (AVATAR.md §13.3): dal file accanto o dal JSON; senza ancore l'avatar non porta accessori.
    const wa = loadWithAnchors(file);
    if (wa.errors.length) throw new Error(id + ': ancore non valide: ' + wa.errors.join('; '));
    let canc = null;
    if (wa.av.anchors) {
      canc = packAnchors(wa.av);
      const u = unpackAnchors(canc);
      if (u.avatarCrc !== crc) throw new Error(id + ': CANC legato a un altro CAVT');
      if (JSON.stringify(u.anchors) !== JSON.stringify(Object.fromEntries(Object.keys(u.anchors).map((p) => [p, wa.av.anchors[p]])))) {
        throw new Error(id + ': andata e ritorno CANC diversa');
      }
    }
    return { id, name: av.name, frames: av.frames, poses: Object.keys(av.poses).length, crc, bin, canc };
  });
}

function accessories() {
  if (!fs.existsSync(ACC_LIB)) return [];
  const ids = fs.readdirSync(ACC_LIB).filter((f) => f.endsWith('.accessory.json')).map((f) => f.slice(0, -'.accessory.json'.length));
  ids.sort((a, b) => {
    const ia = ACC_ORDER.indexOf(a), ib = ACC_ORDER.indexOf(b);
    if (ia >= 0 || ib >= 0) return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib);
    return a < b ? -1 : a > b ? 1 : 0;
  });
  return ids.map((id) => {
    const a = JSON.parse(fs.readFileSync(path.join(ACC_LIB, id + '.accessory.json'), 'utf8'));
    const r = validateAccessory(a);
    if (r.errors.length) throw new Error(id + ': ' + r.errors.join('; '));
    if (a.id !== id) throw new Error(id + ': id diverso dal nome del file');
    const bin = packAccessory(a);
    if (canonicalAccessory(unpackAccessory(bin)) !== canonicalAccessory(a)) throw new Error(id + ': andata e ritorno CACC diversa');
    return { id, name: a.name, crc: bin.readUInt32LE(8), bin };
  });
}

const hex8 = (n) => '0x' + n.toString(16).toUpperCase().padStart(8, '0');

function genJs(avs) {
  let out = '/* Calamaio OS · libreria degli avatar per il simulatore: formato CAVT v1 (docs/AVATAR.md §8) in base64.\n';
  out += ' * Generato da tools/sim-capture/gen-avatars.mjs da avatars/*.avatar.json: NON modificare a mano.\n';
  out += ' * Lo legge assets/calamaio-test.js (loadAvatars), che lo valida come il firmware. */\n';
  out += 'window.CALAMAIO_AVATARS = [\n';
  avs.forEach((a, i) => {
    out += `  { id: '${a.id}', name: '${a.name.replace(/'/g, "\\'")}', crc: ${hex8(a.crc)}, bytes: ${a.bin.length},\n`;
    out += `    cavt: '${a.bin.toString('base64')}' }${i < avs.length - 1 ? ',' : ''}\n`;
  });
  out += '];\n';
  out += '/* Gioco della mascotte (docs/AVATAR.md §13–14): ancore dei predefiniti (CANC v1, legate al CRC del CAVT) e\n';
  out += ' * catalogo degli accessori (CACC v1). Le valida assets/calamaio-test.js come il firmware (gfx/accessory.cpp). */\n';
  out += 'window.CALAMAIO_ANCHORS = [\n';
  const anc = avs.filter((a) => a.canc);
  anc.forEach((a, i) => {
    out += `  { avatar: '${a.id}', avatarCrc: ${hex8(a.crc)}, bytes: ${a.canc.length},\n`;
    out += `    canc: '${a.canc.toString('base64')}' }${i < anc.length - 1 ? ',' : ''}\n`;
  });
  out += '];\n';
  out += 'window.CALAMAIO_ACCESSORIES = [\n';
  accs.forEach((a, i) => {
    out += `  { id: '${a.id}', name: '${a.name.replace(/'/g, "\\'")}', crc: ${hex8(a.crc)}, bytes: ${a.bin.length},\n`;
    out += `    cacc: '${a.bin.toString('base64')}' }${i < accs.length - 1 ? ',' : ''}\n`;
  });
  return out + '];\n';
}

const bytesOf = (buf, ind = '  ') => {
  let out = '';
  for (let i = 0; i < buf.length; i += 20) out += ind + Array.from(buf.subarray(i, i + 20), (b) => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',\n';
  return out;
};
const symOf = (prefix, id) => prefix + id.replace(/(^|-)([a-z0-9])/g, (m, d, c) => c.toUpperCase());

function genAccCpp(avs) {
  let out = '// Ancore dei predefiniti (CANC v1, docs/AVATAR.md §13.4) e catalogo degli accessori (CACC v1, §14.3) compilati\n';
  out += '// nel firmware. Generato da tools/sim-capture/gen-avatars.mjs: NON modificare a mano.\n';
  out += '#include "gfx/accessory_builtin.h"\n\nnamespace {\n\n';
  const anc = avs.filter((a) => a.canc);
  for (const a of anc) {
    out += `// Ancore di ${a.name}: ${a.canc.length} byte, per il CAVT ${hex8(a.crc)}\n`;
    out += `const uint8_t ${symOf('kAnc', a.id)}[${a.canc.length}] = {\n` + bytesOf(a.canc) + '};\n\n';
  }
  for (const a of accs) {
    out += `// ${a.name}: ${a.bin.length} byte, CRC ${hex8(a.crc)}\n`;
    out += `const uint8_t ${symOf('kAcc', a.id)}[${a.bin.length}] = {\n` + bytesOf(a.bin) + '};\n\n';
  }
  out += '}  // namespace\n\n';
  out += 'const BuiltinAnchors kBuiltinAnchors[] = {\n';
  for (const a of anc) out += `  {"${a.id}", ${symOf('kAnc', a.id)}, sizeof ${symOf('kAnc', a.id)}, ${hex8(a.crc)}},\n`;
  out += '};\n';
  out += `const int kBuiltinAnchorsCount = ${anc.length};\n\n`;
  out += 'const BuiltinAccessory kBuiltinAccessories[] = {\n';
  for (const a of accs) out += `  {"${a.id}", ${symOf('kAcc', a.id)}, sizeof ${symOf('kAcc', a.id)}, ${hex8(a.crc)}},\n`;
  out += '};\n';
  out += `const int kBuiltinAccessoryCount = ${accs.length};\n`;
  return out;
}

function genCpp(avs) {
  let out = '// Avatar predefiniti compilati nel firmware: CAVT v1 (docs/AVATAR.md §8) di avatars/*.avatar.json.\n';
  out += '// Generato da tools/sim-capture/gen-avatars.mjs: NON modificare a mano.\n';
  out += '#include "gfx/avatar_builtin.h"\n\nnamespace {\n\n';
  for (const a of avs) {
    const sym = 'k' + a.id.replace(/(^|-)([a-z0-9])/g, (m, d, c) => c.toUpperCase());
    a.sym = sym;
    out += `// ${a.name}: ${a.poses} pose × ${a.frames} fotogrammi, ${a.bin.length} byte, CRC ${hex8(a.crc)}\n`;
    out += `const uint8_t ${sym}[${a.bin.length}] = {\n`;
    for (let i = 0; i < a.bin.length; i += 20) {
      out += '  ' + Array.from(a.bin.subarray(i, i + 20), (b) => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',\n';
    }
    out += '};\n\n';
  }
  out += '}  // namespace\n\n';
  out += `const BuiltinAvatar kBuiltinAvatars[] = {\n`;
  for (const a of avs) out += `  {"${a.id}", ${a.sym}, sizeof ${a.sym}, ${hex8(a.crc)}},\n`;
  out += '};\n';
  out += `const int kBuiltinAvatarCount = ${avs.length};\n`;
  return out;
}

const avs = library();
const accs = accessories();
const files = [[OUT_JS, genJs(avs)], [OUT_CPP, genCpp(avs)], [OUT_ACC_CPP, genAccCpp(avs)]];
if (process.argv.includes('--check')) {
  let stale = 0;
  for (const [f, s] of files) {
    const cur = fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : '';
    if (cur !== s) { console.log('da rigenerare: ' + path.relative(REPO, f)); stale++; }
  }
  if (!stale) console.log('avatar aggiornati: ' + avs.map((a) => `${a.id} ${hex8(a.crc)}`).join(', '));
  process.exit(stale ? 1 : 0);
}
for (const [f, s] of files) { fs.writeFileSync(f, s); console.log('scritto', path.relative(REPO, f), s.length, 'byte'); }
for (const a of avs) console.log(`  ${a.id.padEnd(10)} ${a.poses}×${a.frames}  ${a.bin.length} B  CRC ${hex8(a.crc)}${a.canc ? `  ancore ${a.canc.length} B` : '  senza ancore'}`);
for (const a of accs) console.log(`  accessorio ${a.id.padEnd(10)} ${a.bin.length} B  CRC ${hex8(a.crc)}`);
