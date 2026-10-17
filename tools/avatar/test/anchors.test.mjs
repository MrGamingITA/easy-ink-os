// Test dei punti di aggancio degli avatar (docs/AVATAR.md §13).
// Uso: node --test tools/avatar/test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { load, validate, serialize, POSES, W, H } from '../lib/format.mjs';
import { pack, crc32 } from '../lib/pack.mjs';
import {
  ANCHOR_NAMES, getAnchor, autoAnchors, packAnchors, unpackAnchors, anchorsSize,
  makeSidecar, sidecarErrors, sidecarPath, loadWithAnchors, serializeSidecar
} from '../lib/anchors.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const LIB = ['goccia', 'gatto', 'demone', 'calamaro'];
const libFile = (id) => path.join(root, 'avatars', id + '.avatar.json');
const clone = (o) => JSON.parse(JSON.stringify(o));
const errs = (av) => validate(av, { bytes: Buffer.byteLength(serialize(av)) }).errors;
// CRC del CAVT della libreria (docs/AVATAR.md §12.3): le ancore non devono cambiarli.
const KNOWN_CRC = { goccia: 0x33072CEE, gatto: 0xB2432AD7, demone: 0xA3130345, calamaro: 0x2D86BCB3 };

// Ancore sintetiche: un punto diverso per posa e fotogramma, hand assente nei fotogrammi dispari.
function synthetic(av) {
  const a = {};
  POSES.filter((p) => av.poses[p]).forEach((p, i) => {
    a[p] = av.poses[p].map((_, f) => ({ 'head-top': [12, 4 + f], eyes: [12, 13], mouth: [i, 19], neck: [12, 20], hand: f % 2 ? null : [22, 18 - f] }));
  });
  return a;
}

test('validate accetta un avatar con ancore coerenti, anche con punti null', () => {
  const av = clone(load(libFile('gatto')).av);
  av.anchors = synthetic(av);
  assert.deepEqual(errs(av), []);
  assert.deepEqual(validate(av).warnings, []);
});

test('validate rifiuta ancore incoerenti', () => {
  const base = clone(load(libFile('gatto')).av);
  base.anchors = synthetic(base);
  const cases = [
    ['x fuori', (a) => { a.anchors.done[1].eyes = [W, 3]; }, /anchors\.done\[1\]\.eyes: \(28,3\) fuori dal riquadro 28×25/],
    ['y fuori', (a) => { a.anchors.idle[0]['head-top'] = [3, -1]; }, /anchors\.idle\[0\]\.head-top: \(3,-1\) fuori/],
    ['y oltre', (a) => { a.anchors.idle[0].neck = [3, H]; }, /anchors\.idle\[0\]\.neck: \(3,25\) fuori/],
    ['fotogrammi', (a) => { a.anchors.working = a.anchors.working.slice(0, 2); }, /anchors\.working: servono 4 fotogrammi, trovati 2/],
    ['posa mancante', (a) => { delete a.anchors.error; }, /anchors\.error: mancano le ancore della posa/],
    ['posa senza disegno', (a) => { delete a.poses.stuffy; }, /anchors: posa stuffy assente dal disegno/],
    ['punto ignoto', (a) => { a.anchors.input[2].tail = [1, 1]; }, /anchors\.input\[2\]: punto sconosciuto tail/],
    ['punto mancante', (a) => { delete a.anchors.input[2].mouth; }, /anchors\.input\[2\]: manca mouth/],
    ['non intero', (a) => { a.anchors.run[0].eyes = [1.5, 2]; }, /anchors\.run\[0\]\.eyes: serve \[x, y\] intero oppure null/],
    ['non oggetto', (a) => { a.anchors = []; }, /anchors: deve essere un oggetto/]
  ];
  for (const [name, mut, re] of cases) {
    const a = clone(base); mut(a);
    const e = errs(a);
    assert.ok(e.some((m) => re.test(m)), `${name}: ${e.join(' | ')}`);
  }
});

test('getAnchor: punto, fotogramma f mod frames, ripiego della posa, null', () => {
  const av = clone(load(libFile('gatto')).av);
  assert.equal(getAnchor(av, 'working', 0, 'eyes'), null, 'avatar senza ancore: nessun punto inventato');
  av.anchors = synthetic(av);
  assert.deepEqual(getAnchor(av, 'working', 2, 'head-top'), [12, 6]);
  assert.deepEqual(getAnchor(av, 'working', 6, 'head-top'), [12, 6]);    // 6 mod 4 = 2
  assert.deepEqual(getAnchor(av, 'working', -1, 'head-top'), [12, 7]);   // -1 mod 4 = 3
  assert.equal(getAnchor(av, 'working', 1, 'hand'), null);
  assert.equal(getAnchor(av, 'working', 0, 'coda'), null);
  // tired assente → idle, come il disegno (FALLBACK in format.mjs)
  delete av.poses.tired; delete av.anchors.tired;
  assert.deepEqual(getAnchor(av, 'tired', 0, 'mouth'), av.anchors.idle[0].mouth);
  // il punto restituito è una copia
  getAnchor(av, 'idle', 0, 'eyes')[0] = 99;
  assert.deepEqual(av.anchors.idle[0].eyes, [12, 13]);
});

test('serialize conserva le ancore (avatar dell\'utente in un file solo)', () => {
  const av = clone(load(libFile('demone')).av);
  av.anchors = synthetic(av);
  const back = JSON.parse(serialize(av));
  assert.deepEqual(back.anchors, av.anchors);
  assert.deepEqual(back.poses, av.poses);
  // senza ancore il testo è quello di sempre (i file della libreria non cambiano)
  const plain = load(libFile('demone'));
  assert.equal(serialize(plain.av), fs.readFileSync(libFile('demone'), 'utf8'));
});

test('CAVT: con o senza ancore gli stessi byte, CRC della libreria invariati', () => {
  for (const id of LIB) {
    const { av } = load(libFile(id));
    const withA = loadWithAnchors(libFile(id)).av;
    assert.ok(withA.anchors, `${id}: ancore assenti`);
    const a = pack(av), b = pack(withA);
    assert.ok(a.equals(b), `${id}: il CAVT cambia con le ancore`);
    assert.equal(a.readUInt32LE(8), KNOWN_CRC[id], id);
    assert.equal(a.length, 7072);
  }
});

test('blocco delle ancore (CANC): andata e ritorno, dimensione, legame col CAVT', () => {
  for (const id of LIB) {
    const { av } = loadWithAnchors(libFile(id));
    const buf = packAnchors(av);
    assert.equal(buf.length, anchorsSize(10, 4));
    assert.equal(buf.length, 20 + 10 * 4 * 5 * 2);   // 420 byte
    assert.equal(buf.toString('ascii', 0, 4), 'CANC');
    const back = unpackAnchors(buf);
    assert.equal(back.avatarCrc, KNOWN_CRC[id]);
    assert.equal(back.poseMask, 0x3FF);
    assert.equal(back.frames, 4);
    assert.deepEqual(back.anchors, av.anchors);
  }
  // punti null e avatar a 2 fotogrammi con 6 pose
  const two = clone(load(libFile('demone')).av);
  two.frames = 2;
  for (const p of Object.keys(two.poses)) { if (POSES.indexOf(p) < 6) two.poses[p] = two.poses[p].slice(0, 2); else delete two.poses[p]; }
  two.anchors = synthetic(two);
  assert.deepEqual(errs(two), []);
  const b2 = packAnchors(two);
  assert.equal(b2.length, 20 + 6 * 2 * 10);
  assert.deepEqual(unpackAnchors(b2).anchors, two.anchors);
  assert.equal(unpackAnchors(b2).avatarCrc, pack(two).readUInt32LE(8));
});

test('CANC: rifiuta blocchi rovinati', () => {
  const buf = packAnchors(loadWithAnchors(libFile('gatto')).av);
  const flip = Buffer.from(buf); flip[100] ^= 1;
  assert.throws(() => unpackAnchors(flip), /checksum/);
  assert.throws(() => unpackAnchors(buf.subarray(0, buf.length - 2)), /lunghezza/);
  const magic = Buffer.from(buf); magic[1] = 0;
  assert.throws(() => unpackAnchors(magic), /CANC/);
  const ver = Buffer.from(buf); ver[4] = 2;
  assert.throws(() => unpackAnchors(ver), /versione/);
  const out = Buffer.from(buf); out[20] = 40; out.writeUInt32LE(crc32(out, 12), 8);
  assert.throws(() => unpackAnchors(out), /fuori dal riquadro/);
  const half = Buffer.from(buf); half[20] = 0xFF; half.writeUInt32LE(crc32(half, 12), 8);   // x assente, y presente
  assert.throws(() => unpackAnchors(half), /fuori dal riquadro|incoerente/);
  const noAnchors = load(libFile('gatto')).av;
  assert.throws(() => packAnchors(noAnchors), /senza ancore/);
});

test('ancore della libreria: file accanto, validi, legati al disegno di oggi', () => {
  for (const id of LIB) {
    const file = sidecarPath(libFile(id), id);
    assert.equal(file, path.join(root, 'avatars', 'ancore', id + '.anchors.json'));
    const sc = JSON.parse(fs.readFileSync(file, 'utf8'));
    const { av } = load(libFile(id));
    assert.deepEqual(sidecarErrors(av, sc), [], id);
    assert.equal(fs.readFileSync(file, 'utf8'), serializeSidecar(sc), `${id}: file non nel formato di serializeSidecar`);
    const r = loadWithAnchors(libFile(id));
    assert.equal(r.source, 'sidecar');
    assert.deepEqual(errs(r.av), [], id);
  }
});

test('ancore della libreria, prova di presenza: sede del cappello, occhi, bocca, collo in ogni posa e fotogramma', () => {
  for (const id of LIB) {
    const { av } = loadWithAnchors(libFile(id));
    for (const p of POSES) {
      av.poses[p].forEach((fr, f) => {
        for (const n of ['head-top', 'eyes', 'mouth', 'neck']) assert.ok(getAnchor(av, p, f, n), `${id} ${p}[${f}]: manca ${n}`);
        const [hx, hy] = getAnchor(av, p, f, 'head-top');
        assert.equal(fr[hy][hx], '#', `${id} ${p}[${f}]: head-top (${hx},${hy}) non è sull'inchiostro`);
        // sede della tesa: sopra c'è posto per un cappello alto 5 punti (il Demone, con le corna, ne ha 3)
        assert.ok(hy >= (id === 'demone' ? 2 : 4), `${id} ${p}[${f}]: head-top (${hx},${hy}) troppo in alto per un cappello`);
        const [, ey] = getAnchor(av, p, f, 'eyes'), [, my] = getAnchor(av, p, f, 'mouth'), [, ny] = getAnchor(av, p, f, 'neck');
        assert.ok(hy < ey && ey < my && my < ny, `${id} ${p}[${f}]: ordine verticale testa ${hy} < occhi ${ey} < bocca ${my} < collo ${ny}`);
      });
    }
  }
});

test('sidecar: CRC di un altro disegno, id diverso, ancore in due posti', () => {
  const { av } = load(libFile('gatto'));
  const sc = JSON.parse(fs.readFileSync(sidecarPath(libFile('gatto'), 'gatto'), 'utf8'));
  assert.match(sidecarErrors(av, { ...sc, avatarCrc: '00000000' }).join('\n'), /fatte per un altro disegno/);
  assert.match(sidecarErrors(av, { ...sc, avatar: 'demone' }).join('\n'), /avatar "demone"/);
  assert.match(sidecarErrors({ ...av, anchors: sc.anchors }, sc).join('\n'), /in due posti/);
  const made = makeSidecar(av, sc.anchors);
  assert.deepEqual(made, sc);
});

test('autoAnchors sulla Goccia: punti misurati sul disegno e inseguiti nel salto', () => {
  const { av } = load(libFile('goccia'));
  const a = autoAnchors(av);
  assert.deepEqual(Object.keys(a), POSES);
  // working[0]: punta della goccia (12,4), occhi x 7..10 e 15..18 dalla riga 13, bocca alla riga 19
  assert.deepEqual(a.working[0]['head-top'], [12, 4]);
  assert.deepEqual(a.working[0].eyes, [12, 13]);
  assert.deepEqual(a.working[0].mouth, [12, 19]);
  assert.deepEqual(a.working[0].neck, [12, 20]);
  // done[2] salta di 3 punti: tutto sale di 3
  assert.deepEqual(a.done[2]['head-top'], [12, 1]);
  assert.deepEqual(a.done[2].eyes, [12, 10]);
  // error scende di 1
  assert.deepEqual(a.error[0].eyes, [12, 14]);
  for (const p of POSES) for (const pt of a[p]) assert.deepEqual(Object.keys(pt), ANCHOR_NAMES);
  // le ancore automatiche sono valide
  assert.deepEqual(errs({ ...av, anchors: a }), []);
});
