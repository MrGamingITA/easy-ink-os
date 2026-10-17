// Test del catalogo iniziale degli accessori (avatars/accessori/, docs/AVATAR.md §14.5).
// Uso: node --test tools/avatar/test/*.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { validateAccessory, packAccessory, unpackAccessory, canonicalAccessory, serializeAccessory } from '../lib/accessory.mjs';
import { checkAccessory, drawAccessory, toGrid, fromGrid } from '../lib/draw-accessory.mjs';
import { loadWithAnchors } from '../lib/anchors.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const dir = path.join(root, 'avatars', 'accessori');
const CATALOG = ['cappellino', 'occhiali', 'sciarpa', 'papillon', 'corona', 'cuffie', 'fiocco', 'mago'];
const LIB = ['goccia', 'gatto', 'demone', 'calamaro'];
const avs = LIB.map((id) => loadWithAnchors(path.join(root, 'avatars', id + '.avatar.json')).av);
const read = (id) => JSON.parse(fs.readFileSync(path.join(dir, id + '.accessory.json'), 'utf8'));

test('catalogo: gli 8 accessori della specifica, validi, CACC ≤ 1 KB con andata e ritorno', () => {
  assert.deepEqual(fs.readdirSync(dir).filter((f) => f.endsWith('.accessory.json')).map((f) => f.replace('.accessory.json', '')).sort(), [...CATALOG].sort());
  for (const id of CATALOG) {
    const a = read(id);
    assert.equal(a.id, id);
    assert.equal(a.special, false);
    const r = validateAccessory(a);
    assert.deepEqual(r.errors, [], id);
    assert.deepEqual(r.warnings, [], id);
    const bin = packAccessory(a);
    assert.ok(bin.length <= 1024, id);
    assert.equal(canonicalAccessory(unpackAccessory(bin)), canonicalAccessory(a), id);
    assert.equal(fs.readFileSync(path.join(dir, id + '.accessory.json'), 'utf8'), serializeAccessory(a), `${id}: file non nel formato di serializeAccessory`);
  }
});

test('catalogo: nessun problema di check su Goccia, Gatto, Demone, Calamaro in tutte le pose', () => {
  assert.ok(avs.every((av) => av.anchors), 'la libreria ha le ancore');
  for (const id of CATALOG) assert.deepEqual(checkAccessory(read(id), avs), [], id);
});

test('catalogo, prova di presenza: ogni accessorio cambia il disegno di ogni avatar in working[0]', () => {
  for (const id of CATALOG) {
    const a = read(id);
    for (const av of avs) {
      const g = toGrid(av.poses.working[0]), before = fromGrid(g).join('\n');
      const res = drawAccessory(g, av, a, 'working', 0);
      assert.ok(res && res.cells.length >= 6, `${id} su ${av.id}: non disegnato`);
      assert.notEqual(fromGrid(g).join('\n'), before, `${id} su ${av.id}: nessun pixel cambiato`);
    }
  }
});

test('catalogo: la sciarpa ha 2 fotogrammi, gli occhiali spariscono dove gli occhi non sono aperti', () => {
  assert.equal(read('sciarpa').frames, 2);
  for (const p of ['error', 'idle', 'tired']) assert.ok(read('occhiali').hidePoses.includes(p), p);
  const g = toGrid(avs[0].poses.idle[0]);
  assert.equal(drawAccessory(g, avs[0], read('occhiali'), 'idle', 0), null);
});
