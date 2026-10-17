#!/usr/bin/env node
// Golden della parità C++/JS dei temi (docs/TEMI.md §7). Per ogni tema di protocol/themes/fixtures × schermata × scenario di
// protocol/themes/scenarios.json disegna con render.js e scrive firmware/test/test_theme_parity/golden.json (CRC-32
// dell'immagine 250×122 nel formato di Canvas::data(), drawn, nextChange, riquadro della mascotte). Il test C++ rifà
// gli stessi casi sul CTHM compilato dal servizio.
//   node tools/golden/theme-golden.mjs            scrive golden.json
//   node tools/golden/theme-golden.mjs --check    esce con 1 se golden.json non corrisponde al renderer di oggi
//   node tools/golden/theme-golden.mjs --pbm DIR  scrive anche le immagini JS (DIR/<tema>-<schermata>-<scenario>-js.pbm)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadWithAnchors } from '../avatar/lib/anchors.mjs';
import { lowerTheme } from '../../service/internal/webui/static/assets/js/theme/lower.js';
import { renderScreen } from '../../service/internal/webui/static/assets/js/theme/render.js';
import { makeModel } from '../../service/internal/webui/static/assets/js/theme/values.js';
import { Bitmap, crc32 } from '../../service/internal/webui/static/assets/js/theme/surface.js';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const OUT = path.join(REPO, 'firmware', 'test', 'test_theme_parity', 'golden.json');
const FX = path.join(REPO, 'protocol', 'themes', 'fixtures');
const THEMES = ['prova', 'elementi', 'lista'];

const avs = {};
for (const f of fs.readdirSync(path.join(REPO, 'avatars')).filter((f) => f.endsWith('.avatar.json'))) {
  const { av } = loadWithAnchors(path.join(REPO, 'avatars', f));
  avs[av.id] = av;
}
const accs = {};
for (const f of fs.readdirSync(path.join(REPO, 'avatars', 'accessori')).filter((f) => f.endsWith('.accessory.json'))) {
  const a = JSON.parse(fs.readFileSync(path.join(REPO, 'avatars', 'accessori', f), 'utf8'));
  accs[a.id] = a;
}
const assets = { avatar: (id) => avs[id] || null, accessory: (id) => accs[id] || null };

const sc = JSON.parse(fs.readFileSync(path.join(REPO, 'protocol', 'themes', 'scenarios.json'), 'utf8'));
const byName = Object.fromEntries(sc.scenarios.map((s) => [s.name, s]));
const merge = (a, b) => { const o = { ...a }; for (const [k, v] of Object.entries(b)) o[k] = v && typeof v === 'object' && !Array.isArray(v) && a[k] && typeof a[k] === 'object' ? merge(a[k], v) : v; return o; };

const pbmDir = process.argv.includes('--pbm') ? process.argv[process.argv.indexOf('--pbm') + 1] : null;
const cases = [];
for (const t of THEMES) {
  const lowered = lowerTheme(JSON.parse(fs.readFileSync(path.join(FX, t + '.theme.json'), 'utf8')));
  for (const screen of Object.keys(lowered.screens).sort()) {
    for (const s of sc.scenarios) {
      if (s.extends && !byName[s.extends]) throw new Error('scenario ' + s.name + ': extends ' + s.extends + ' non esiste');
      // base → extends → scenario; now è quello finale, e da lì since e sel (come il test C++)
      const over = s.extends ? merge(byName[s.extends], s) : s;
      const m = makeModel(sc.base, over);
      const b = new Bitmap();
      const info = renderScreen(lowered, screen, m, b, assets);
      const packed = b.packed();
      cases.push({ theme: t, screen, scenario: s.name, drawn: info.drawn, next: info.nextChange,
        mascot: info.hasMascot ? [info.mx, info.my, info.mw, info.mh] : null, crc: crc32(packed) });
      if (pbmDir) {
        fs.mkdirSync(pbmDir, { recursive: true });
        fs.writeFileSync(path.join(pbmDir, t + '-' + screen + '-' + s.name + '-js.pbm'), Buffer.concat([Buffer.from('P4\n250 122\n'), Buffer.from(packed)]));
      }
    }
  }
}
// un caso per riga: le differenze si leggono nel diff
const text = '{\n "comment": "Generato da tools/golden/theme-golden.mjs: non modificare a mano.",\n "cases": [\n' +
  cases.map((c) => '  ' + JSON.stringify(c)).join(',\n') + '\n ]\n}\n';
if (process.argv.includes('--check')) {
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
  if (old !== text) { console.error('golden.json non corrisponde al renderer: node tools/golden/theme-golden.mjs, poi pio test -f test_theme_parity'); process.exit(1); }
  console.log('golden dei temi aggiornato: ' + cases.length + ' casi');
} else {
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ': ' + cases.length + ' casi');
}
