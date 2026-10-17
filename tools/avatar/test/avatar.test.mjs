// Test del validatore, del formato binario e della scelta degli avatar liberi.
// Uso: node --test tools/avatar/test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { load, validate, serialize, poseFrames, POSES, REQUIRED, MIN_POSE_DIFF, W, H, usage, freeChoices } from '../lib/format.mjs';
import { pack, unpack, packedSize, crc32, HEADER } from '../lib/pack.mjs';
import { goccia } from '../goccia-export.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const LIB = ['goccia', 'gatto', 'demone', 'calamaro'];
const libFile = (id) => path.join(root, 'avatars', id + '.avatar.json');
const clone = (o) => JSON.parse(JSON.stringify(o));
const errs = (av) => validate(av, { bytes: Buffer.byteLength(serialize(av)) }).errors;

test('la libreria è valida, senza avvisi, e ogni file supera i controlli di peso', () => {
  for (const id of LIB) {
    const { av, bytes } = load(libFile(id));
    const r = validate(av, { bytes });
    assert.deepEqual(r.errors, [], id);
    assert.deepEqual(r.warnings, [], id);
    assert.equal(av.id, id);
    assert.equal(r.stats.poses, 10);
    assert.ok(av.poses.run, `${id}: manca la corsa`);
    assert.ok(pack(av).length <= 16 * 1024);
  }
});

test('goccia.avatar.json coincide con mascot() eseguita ora dal simulatore', () => {
  const { av } = load(libFile('goccia'));
  assert.deepEqual(av.poses, goccia().poses);
});

test('prova di presenza: la Goccia ha inchiostro, carta disegnata e trasparenza', () => {
  const fr = load(libFile('goccia')).av.poses.permission[1].join('');
  for (const ch of '#+.') assert.ok(fr.includes(ch), `manca ${ch}`);
});

test('errori di struttura', () => {
  const base = load(libFile('gatto')).av;
  const cases = [
    ['format', (a) => { a.format = 'altro'; }, /format/],
    ['version', (a) => { a.version = 2; }, /version/],
    ['id', (a) => { a.id = 'Gatto_Nero'; }, /id/],
    ['id lungo', (a) => { a.id = 'a'.repeat(25); }, /id/],
    ['name lungo', (a) => { a.name = 'è'.repeat(13); }, /name/],   // 26 byte UTF-8
    ['description', (a) => { a.description = 'x'.repeat(121); }, /description/],
    ['w', (a) => { a.w = 27; }, /w deve/],
    ['frames', (a) => { a.frames = 3; }, /frames/],
    ['campo extra', (a) => { a.colore = 'rosso'; }, /campo sconosciuto/],
    ['posa ignota', (a) => { a.poses.dance = a.poses.idle; }, /posa sconosciuta/],
    ['posa mancante', (a) => { delete a.poses.error; }, /obbligatoria mancante: error/],
    ['fotogrammi', (a) => { a.poses.done = a.poses.done.slice(0, 2); }, /servono 4 fotogrammi/],
    ['righe', (a) => { a.poses.idle[0] = a.poses.idle[0].slice(1); }, /servono 25 righe/],
    ['colonne', (a) => { a.poses.idle[0][3] += '.'; }, /servono 28 simboli/],
    ['simboli', (a) => { a.poses.idle[0][3] = 'x'.repeat(W); }, /simboli ammessi/]
  ];
  for (const [name, mut, re] of cases) {
    const a = clone(base); mut(a);
    const e = errs(a);
    assert.ok(e.some((m) => re.test(m)), `${name}: ${e.join(' | ')}`);
  }
});

test('peso del JSON oltre il limite', () => {
  const { av } = load(libFile('gatto'));
  assert.ok(validate(av, { bytes: 49 * 1024 }).errors.some((m) => /file JSON/.test(m)));
});

test('silhouette vuota e pose indistinguibili', () => {
  const a = clone(load(libFile('gatto')).av);
  a.poses.tired = a.poses.idle.map((fr) => fr.map((r) => r));
  a.poses.tired[0][0] = '#' + a.poses.tired[0][0].slice(1);   // differenza di un pixel solo
  assert.ok(errs(a).some((m) => /idle e tired differiscono di 1 pixel/.test(m)));
  const b = clone(load(libFile('gatto')).av);
  b.poses.stuffy[2] = Array(H).fill('.'.repeat(W));
  assert.ok(errs(b).some((m) => /stuffy\[2\]: silhouette vuota/.test(m)));
  assert.ok(MIN_POSE_DIFF >= 8);
});

test('avvisi: retino fitto, fotogrammi 0-1 uguali, nuvoletta assente', () => {
  const a = clone(load(libFile('calamaro')).av);
  a.poses.idle[0] = a.poses.idle[0].map((r, y) => (y > 18 ? Array.from(r, (c, x) => ((x + y) % 2 ? '#' : '+')).join('') : r));
  a.poses.input[1] = a.poses.input[0].slice();
  a.poses.working = a.poses.working.map((fr) => fr.map((r, y) => (y <= 8 ? '.'.repeat(17) + r.slice(17).replace(/[#+]/g, '.') : r)));
  const w = validate(a).warnings.join('\n');
  assert.match(w, /idle\[0\]: \d+ blocchi a scacchiera/);
  assert.match(w, /input: i fotogrammi 0 e 1 sono uguali/);
  assert.match(w, /working: angolo in alto a destra vuoto/);
  const b = clone(load(libFile('goccia')).av);
  b.poses.permission[1][8] = b.poses.permission[1][8].slice(0, 20) + '#' + b.poses.permission[1][8].slice(21);
  assert.match(validate(b).warnings.join('\n'), /permission\[1\]: 1 pixel nell'angolo della nuvoletta fuori dal suo ingombro \(20,8\)/);
});

test('avatar minimo: solo le 6 pose obbligatorie, 2 fotogrammi', () => {
  const a = clone(load(libFile('demone')).av);
  a.frames = 2;
  for (const p of POSES) { if (REQUIRED.includes(p)) a.poses[p] = a.poses[p].slice(0, 2); else delete a.poses[p]; }
  assert.deepEqual(errs(a), []);
  const buf = pack(a);
  assert.equal(buf.length, packedSize(6, 2));
  assert.equal(buf.length, HEADER + 6 * 2 * 175);
  const back = unpack(buf);
  assert.deepEqual(Object.keys(back.poses), REQUIRED);
});

test('pack: andata e ritorno identica per tutta la libreria', () => {
  for (const id of LIB) {
    const { av } = load(libFile(id));
    const buf = pack(av);
    assert.equal(buf.length, 72 + 40 * 175);   // 7072 byte: 10 pose × 4 fotogrammi
    const back = unpack(buf);
    assert.equal(back.id, av.id); assert.equal(back.name, av.name); assert.equal(back.frames, 4);
    assert.deepEqual(back.poses, av.poses);
  }
});

test('pack: intestazione e codifica dei pixel', () => {
  const a = clone(load(libFile('goccia')).av);
  const buf = pack(a);
  assert.equal(buf.toString('ascii', 0, 4), 'CAVT');
  assert.equal(buf[4], 1); assert.equal(buf[5], 72); assert.equal(buf.readUInt16LE(6), buf.length);
  assert.equal(buf[12], 28); assert.equal(buf[13], 25); assert.equal(buf[14], 4);
  assert.equal(buf.readUInt16LE(16), 0x3FF); assert.equal(buf.readUInt16LE(18), 175);   // bit 9 = run
  assert.equal(buf.readUInt32LE(8), crc32(buf, 12));
  // pixel (0,0) di working[0] nei bit 7-6 del primo byte; '.'=0, '#'=1, '+'=2
  a.poses.working[0][0] = '#+.' + a.poses.working[0][0].slice(3);
  const b = pack(a);
  assert.equal(b[72] >> 2, 0b011000);
});

test('unpack rifiuta binari rovinati', () => {
  const buf = pack(load(libFile('gatto')).av);
  const flip = Buffer.from(buf); flip[500] ^= 1;
  assert.throws(() => unpack(flip), /checksum/);
  assert.throws(() => unpack(buf.subarray(0, buf.length - 1)), /lunghezza/);
  const magic = Buffer.from(buf); magic[0] = 0;
  assert.throws(() => unpack(magic), /CAVT/);
  const ver = Buffer.from(buf); ver[4] = 2;
  assert.throws(() => unpack(ver), /versione/);
  const three = Buffer.from(buf); three[72] |= 0xC0; three.writeUInt32LE(crc32(three, 12), 8);
  assert.throws(() => unpack(three), /riservato/);
});

test('crc32 standard (vettore noto)', () => {
  assert.equal(crc32(Buffer.from('123456789')), 0xCBF43926);
});

test('avatar liberi: in uso = assegnato a una sessione aperta, non archiviata, diversa dalla corrente', () => {
  const st = JSON.parse(fs.readFileSync(path.join(root, 'tools/avatar/test/stato-esempio.json'), 'utf8'));
  const lib = LIB.map((id) => ({ id, name: id }));
  const u = Object.fromEntries(usage(lib, st.assignments, st.sessions, 'sess-corrente').map((x) => [x.id, x]));
  assert.equal(u.gatto.state, 'in-uso'); assert.deepEqual(u.gatto.usedBy, ['api-server']);   // per progetto
  assert.equal(u.demone.state, 'libero');                                                      // solo una sessione archiviata
  assert.equal(u.calamaro.state, 'libero');                                                   // assegnato solo alla sessione corrente
  assert.equal(u.goccia.state, 'in-uso');                                                     // web-app non ha assegnazioni
  assert.deepEqual(freeChoices(lib, st.assignments, st.sessions, 'sess-corrente').map((x) => x.id), ['demone', 'calamaro']);
  // la Goccia non è mai proposta, nemmeno quando è libera
  assert.ok(!freeChoices(lib, {}, [], null).some((x) => x.id === 'goccia'));
});

// ---- posa run (docs/AVATAR.md §12) ----

test('run: è la 10ª posa, facoltativa, col ripiego su working', () => {
  assert.equal(POSES.indexOf('run'), 9);
  assert.ok(!REQUIRED.includes('run'));
  const a = clone(load(libFile('gatto')).av);
  delete a.poses.run;
  assert.deepEqual(errs(a), []);
  assert.deepEqual(poseFrames(a, 'run'), a.poses.working);
  assert.equal(pack(a).readUInt16LE(16), 0x1FF);
});

test('run: prova di presenza, ogni avatar corre verso destra coi piedi sulla barra', () => {
  for (const id of LIB) {
    const fr = load(libFile(id)).av.poses.run;
    fr.forEach((rows, f) => {
      assert.ok(rows[24].includes('#'), `${id} run[${f}]: piedi sulla riga 24`);
      assert.ok(rows.filter((r) => r[27] === '#').length >= 3, `${id} run[${f}]: bordo destro in colonna 27`);
    });
    // 0 e 1 diversi (profilo ridotto) e il fotogramma 0 diverso da working (non è il ripiego travestito)
    assert.notDeepEqual(fr[0], fr[1], id);
    assert.notDeepEqual(fr[0], load(libFile(id)).av.poses.working[0], id);
  }
});

test('run: avvisi di leggibilità (bordo destro, piedi, passo, sagoma)', () => {
  const base = load(libFile('goccia')).av;
  const warn = (mut) => { const a = clone(base); mut(a.poses.run); return validate(a).warnings.join('\n'); };
  // bordo destro spostato di un punto a sinistra in un fotogramma
  assert.match(warn((r) => { r[2] = r[2].map((row) => row.slice(1) + '.'); }), /run\[2\]: bordo destro in colonna 26/);
  // piedi sollevati: la riga 24 svuotata
  assert.match(warn((r) => { r[1][24] = '.'.repeat(W); }), /run\[1\]: niente inchiostro sulla riga 24/);
  // passo fermo: 1 uguale a 0 salvo pochi pixel
  assert.match(warn((r) => { r[1] = r[0].slice(); }), /run: solo 0 pixel cambiano tra i fotogrammi 0 e 1/);
  // sagoma che pulsa: un fotogramma dimezzato
  assert.match(warn((r) => { r[3] = r[3].map((row, y) => (y < 14 ? row.replace(/#/g, '.') : row)); }), /run: l'inchiostro varia/);
  // la libreria non ne ha nessuno
  assert.deepEqual(validate(base).warnings, []);
  // la corsa deve distinguersi dalle altre pose come tutte (errore, non avviso)
  const same = clone(base); same.poses.run = clone(same.poses.working);
  assert.ok(errs(same).some((m) => /working e run differiscono di 0 pixel/.test(m)));
});

test('CAVT con run: 7 072 byte, le prime 9 pose sono gli stessi byte del CAVT senza run', () => {
  for (const id of LIB) {
    const av = load(libFile(id)).av;
    const nine = clone(av); delete nine.poses.run;
    const b10 = pack(av), b9 = pack(nine);
    assert.equal(b10.length, packedSize(10, 4)); assert.equal(b10.length, 7072);
    assert.equal(b9.length, 6372);
    // stessa intestazione salvo lunghezza, CRC e bit 9; stessi fotogrammi nelle stesse posizioni
    assert.equal(b10.readUInt16LE(16), b9.readUInt16LE(16) | (1 << 9));
    assert.ok(b10.subarray(12, 16).equals(b9.subarray(12, 16)) && b10.subarray(18, HEADER).equals(b9.subarray(18, HEADER)));
    assert.ok(b10.subarray(HEADER, b9.length).equals(b9.subarray(HEADER)), `${id}: le pose 0-8 si sono spostate`);
    // un lettore v1 che conosce 9 pose e ignora i bit più alti (offset = pose presenti prima × fotogrammi × 175)
    const mask = b10.readUInt16LE(16) & 0x1FF, back = unpack(b10);
    POSES.slice(0, 9).forEach((p, i) => {
      if (!(mask & (1 << i))) return;
      let n = 0; for (let k = 0; k < i; k++) if (mask & (1 << k)) n++;
      const off = HEADER + n * 4 * 175;
      assert.ok(b10.subarray(off, off + 4 * 175).equals(b9.subarray(off, off + 4 * 175)), `${id} ${p}`);
      assert.deepEqual(back.poses[p], av.poses[p]);
    });
    assert.deepEqual(back.poses.run, av.poses.run);
  }
});

test('unpack: bit 9 accettato, bit oltre il 9 rifiutati', () => {
  const buf = Buffer.from(pack(load(libFile('demone')).av));
  buf.writeUInt16LE(buf.readUInt16LE(16) | (1 << 10), 16); buf.writeUInt32LE(crc32(buf, 12), 8);
  assert.throws(() => unpack(buf), /bit di posa sconosciuti/);
  const two = clone(load(libFile('demone')).av);
  two.frames = 2; for (const p of Object.keys(two.poses)) two.poses[p] = two.poses[p].slice(0, 2);
  assert.deepEqual(errs(two), []);
  assert.equal(pack(two).length, 72 + 10 * 2 * 175);   // 3 572 byte
});
