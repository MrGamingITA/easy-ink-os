#!/usr/bin/env node
// Strumenti per gli avatar Calamaio (Node 22, nessuna dipendenza). Specifica: docs/AVATAR.md.
//   node tools/avatar/avatar.mjs validate <file...>          controlla formato e leggibilità
//   node tools/avatar/avatar.mjs preview <file> [--png out.png] [--home out.png] [--text] [--pose p]
//   node tools/avatar/avatar.mjs compare <out.png> <file...>  foglio di confronto (pose × avatar)
//   node tools/avatar/avatar.mjs pack <file> [out.cavt]       formato binario CAVT v1 (con verifica di ritorno)
//   node tools/avatar/avatar.mjs unpack <in.cavt> [out.json]
//   node tools/avatar/avatar.mjs bar <file> <out.png> [--frames2]  corsa sulla barra dell'aggiornamento (6 istantanee, scala 1 e 2)
//   node tools/avatar/avatar.mjs sim <out.html> <file...>     pagina statica che anima come il display
//   node tools/avatar/avatar.mjs build <design.mjs> [out.json] genera l'avatar da un disegno procedurale
//   node tools/avatar/avatar.mjs free <stato.json> [--session ID] [--dir avatars]  avatar liberi (simulazione)
//   node tools/avatar/avatar.mjs anchors <file> --auto [--force]  propone le ancore e le scrive in ancore/<id>.anchors.json
//   node tools/avatar/avatar.mjs anchors <file> [--png out.png] [--pack out.canc]  controlla le ancore, foglio, blocco CANC
//   node tools/avatar/avatar.mjs accessory validate <file...>       controlla gli accessori (docs/AVATAR.md §14)
//   node tools/avatar/avatar.mjs accessory pack <file> [out.cacc]   binario CACC v1 (con verifica di ritorno)
//   node tools/avatar/avatar.mjs accessory unpack <in.cacc> [out.json]
//   node tools/avatar/avatar.mjs accessory check <file...> [--avatars dir]   fuori riquadro, nuvoletta, contrasto su ogni avatar e posa
//   node tools/avatar/avatar.mjs accessory preview <file> --png out.png [--avatars dir] [--text --avatar id --pose p]
//   node tools/avatar/avatar.mjs library                     rigenera avatars/ (Goccia dal simulatore, disegni) e avatars/anteprime/
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { load, validate, serialize, POSES, usage, freeChoices } from './lib/format.mjs';
import { pack, unpack } from './lib/pack.mjs';
import { sheet, compare, homeMock, text, barSheet, anchorSheet, accessorySheet, accessoryCatalog } from './lib/render.mjs';
import { checkAccessory, renderWith } from './lib/draw-accessory.mjs';
import { autoAnchors, loadWithAnchors, makeSidecar, serializeSidecar, sidecarPath, packAnchors, unpackAnchors } from './lib/anchors.mjs';
import { simHtml } from './lib/sim.mjs';
import { validateAccessory, packAccessory, unpackAccessory, serializeAccessory, canonicalAccessory } from './lib/accessory.mjs';
import { goccia } from './goccia-export.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [cmd, ...args] = process.argv.slice(2);
const flag = (name) => { const i = args.indexOf(name); if (i < 0) return undefined; const v = args[i + 1]; args.splice(i, 2); return v; };
const bool = (name) => { const i = args.indexOf(name); if (i < 0) return false; args.splice(i, 1); return true; };
const die = (m) => { console.error(m); process.exit(2); };
// Libreria degli avatar con le ancore, Goccia per prima (come library).
function avatarsIn(dir) {
  const files = fs.readdirSync(dir).filter((f) => f.endsWith('.avatar.json')).sort((a, b) => (a === 'goccia.avatar.json' ? -1 : b === 'goccia.avatar.json' ? 1 : a.localeCompare(b)));
  return files.map((f) => loadWithAnchors(path.join(dir, f)).av);
}

function check(file) {
  const { av, bytes } = load(file);
  const r = validate(av, { bytes });
  return { av, r };
}

async function main() {
  if (cmd === 'validate') {
    if (!args.length) die('uso: validate <file...>');
    let bad = 0;
    for (const f of args) {
      let res;
      try { res = check(f); } catch (e) { console.log(`✗ ${f}: ${e.message}`); bad++; continue; }
      const { av, r } = res;
      if (r.errors.length) bad++;
      let bin = '';
      if (!r.errors.length) bin = `, binario ${pack(av).length} B`;
      console.log(`${r.errors.length ? '✗' : '✓'} ${f}: ${r.stats.poses || 0} pose, JSON ${r.stats.jsonBytes} B${bin}, differenza minima tra pose ${r.stats.minPoseDiff} px (${r.stats.closest}), inchiostro minimo ${r.stats.minInk} px`);
      r.errors.forEach((e) => console.log('  errore: ' + e));
      r.warnings.forEach((w) => console.log('  avviso: ' + w));
    }
    process.exit(bad ? 1 : 0);
  }
  if (cmd === 'preview') {
    const png = flag('--png'), home = flag('--home'), pose = flag('--pose'), asText = bool('--text');
    const { av } = load(args[0] || die('uso: preview <file> [--png out] [--home out] [--text] [--pose p]'));
    if (png) { fs.writeFileSync(png, sheet(av)); console.log('scritto', png); }
    if (home) { fs.writeFileSync(home, homeMock(av, pose || 'working', 0)); console.log('scritto', home); }
    if (asText || (!png && !home)) console.log(text(av, pose ? [pose] : POSES));
    return;
  }
  if (cmd === 'compare') {
    const out = args.shift();
    if (!out || !args.length) die('uso: compare <out.png> <file...>');
    fs.writeFileSync(out, compare(args.map((f) => load(f).av)));
    console.log('scritto', out);
    return;
  }
  if (cmd === 'pack') {
    const { av, r } = check(args[0] || die('uso: pack <file> [out]'));
    if (r.errors.length) die('avatar non valido:\n  ' + r.errors.join('\n  '));
    const buf = pack(av), back = unpack(buf);
    for (const p of POSES) if (JSON.stringify(back.poses[p]) !== JSON.stringify(av.poses[p])) die(`andata e ritorno diversa in ${p}`);
    const out = args[1] || args[0].replace(/\.avatar\.json$|\.json$/, '') + '.cavt';
    fs.writeFileSync(out, buf);
    console.log(`scritto ${out}: ${buf.length} byte, crc32 ${buf.readUInt32LE(8).toString(16).padStart(8, '0')}, andata e ritorno identica`);
    return;
  }
  if (cmd === 'unpack') {
    const av = unpack(fs.readFileSync(args[0] || die('uso: unpack <in.cavt> [out.json]')));
    av.author = '(dal binario)'; av.description = '';
    const s = serialize(av);
    if (args[1]) { fs.writeFileSync(args[1], s); console.log('scritto', args[1]); } else process.stdout.write(s);
    return;
  }
  if (cmd === 'bar') {
    const frames2 = bool('--frames2');
    const [file, out] = args;
    if (!file || !out) die('uso: bar <file> <out.png> [--frames2]');
    fs.writeFileSync(out, barSheet(load(file).av, { frames2 }));
    console.log('scritto', out);
    return;
  }
  if (cmd === 'sim') {
    const out = args.shift();
    if (!out || !args.length) die('uso: sim <out.html> <file...>');
    fs.writeFileSync(out, simHtml(args.map((f) => load(f).av)));
    console.log('scritto', out);
    return;
  }
  if (cmd === 'build') {
    const mod = await import(pathToFileURL(path.resolve(args[0] || die('uso: build <design.mjs> [out]'))).href);
    const av = mod.default();
    const r = validate(av, { bytes: Buffer.byteLength(serialize(av)) });
    const out = args[1] || path.join(root, 'avatars', av.id + '.avatar.json');
    fs.writeFileSync(out, serialize(av));
    console.log(`scritto ${out}: ${r.errors.length} errori, ${r.warnings.length} avvisi`);
    r.errors.forEach((e) => console.log('  errore: ' + e));
    r.warnings.forEach((w) => console.log('  avviso: ' + w));
    process.exit(r.errors.length ? 1 : 0);
  }
  if (cmd === 'library') {
    const dir = path.join(root, 'avatars'), prev = path.join(dir, 'anteprime');
    fs.mkdirSync(prev, { recursive: true });
    fs.writeFileSync(path.join(dir, 'goccia.avatar.json'), serialize(goccia()));
    const designs = path.join(path.dirname(fileURLToPath(import.meta.url)), 'designs');
    for (const d of fs.readdirSync(designs).filter((f) => f.endsWith('.mjs')).sort()) {
      const av = (await import(pathToFileURL(path.join(designs, d)).href)).default();
      fs.writeFileSync(path.join(dir, av.id + '.avatar.json'), serialize(av));
    }
    const files = fs.readdirSync(dir).filter((f) => f.endsWith('.avatar.json')).sort((a, b) => (a === 'goccia.avatar.json' ? -1 : b === 'goccia.avatar.json' ? 1 : a.localeCompare(b)));
    const avs = files.map((f) => load(path.join(dir, f)).av);
    let bad = 0;
    for (const f of files) {
      const { av, r } = check(path.join(dir, f));
      if (r.errors.length) bad++;
      console.log(`${r.errors.length ? '✗' : '✓'} ${f} (${r.errors.length} errori, ${r.warnings.length} avvisi)`);
      fs.writeFileSync(path.join(prev, av.id + '.png'), sheet(av));
      fs.writeFileSync(path.join(prev, 'barra-' + av.id + '.png'), barSheet(av));
      const wa = loadWithAnchors(path.join(dir, f));
      if (wa.errors.length) { bad++; console.log('  ancore: ' + wa.errors.join('\n  ancore: ')); }
      else if (wa.source) fs.writeFileSync(path.join(prev, 'ancore-' + av.id + '.png'), anchorSheet(wa.av));
    }
    fs.writeFileSync(path.join(prev, 'confronto.png'), compare(avs));
    // Accessori (avatars/accessori/): un foglio per accessorio su tutti gli avatar e il catalogo.
    const accDir = path.join(dir, 'accessori');
    if (fs.existsSync(accDir)) {
      const withA = avatarsIn(dir), accs = [];
      for (const f of fs.readdirSync(accDir).filter((x) => x.endsWith('.accessory.json')).sort()) {
        const a = JSON.parse(fs.readFileSync(path.join(accDir, f), 'utf8')), v = validateAccessory(a);
        const probs = v.errors.length ? v.errors : checkAccessory(a, withA);
        if (probs.length) { bad++; console.log(`✗ accessori/${f}: ${probs.join(' | ')}`); continue; }
        console.log(`✓ accessori/${f}`);
        fs.writeFileSync(path.join(prev, 'accessori-' + a.id + '.png'), accessorySheet(a, withA));
        accs.push(a);
      }
      if (accs.length) fs.writeFileSync(path.join(prev, 'accessori-catalogo.png'), accessoryCatalog(accs, withA));
    }
    fs.writeFileSync(path.join(prev, 'avatar-sim.html'), simHtml(avs));
    console.log('anteprime in', prev);
    process.exit(bad ? 1 : 0);
  }
  if (cmd === 'anchors') {
    const png = flag('--png'), out = flag('--pack'), auto = bool('--auto'), force = bool('--force');
    const file = args[0] || die('uso: anchors <file> --auto [--force] | anchors <file> [--png out.png] [--pack out.canc]');
    if (auto) {
      const { av } = load(file);
      if (av.anchors && !force) die(`${file} ha già le ancore nel JSON: --force per rifarle`);
      const sp = sidecarPath(file, av.id);
      if (fs.existsSync(sp) && !force) die(`${sp} esiste già (forse corretto a mano): --force per sovrascriverlo`);
      fs.mkdirSync(path.dirname(sp), { recursive: true });
      fs.writeFileSync(sp, serializeSidecar(makeSidecar(av, autoAnchors(av))));
      console.log(`scritto ${sp}: proposta automatica, da rivedere sul foglio (anchors ${file} --png out.png)`);
      return;
    }
    const r = loadWithAnchors(file);
    if (r.errors.length) die(`✗ ${file}:\n  ` + r.errors.join('\n  '));
    if (!r.source) { console.log(`${file}: nessuna ancora (gli accessori non si disegnano su questo avatar)`); }
    else {
      const bin = packAnchors(r.av), back = unpackAnchors(bin);
      if (JSON.stringify(back.anchors) !== JSON.stringify(r.av.anchors)) die('andata e ritorno del blocco CANC diversa');
      let nul = 0, tot = 0;
      for (const frs of Object.values(r.av.anchors)) for (const pt of frs) for (const v of Object.values(pt)) { tot++; if (!v) nul++; }
      console.log(`✓ ${file}: ancore da ${r.source === 'json' ? 'JSON' : r.sidecar}, ${tot} punti (${nul} assenti), blocco CANC ${bin.length} B per il CAVT ${back.avatarCrc.toString(16).padStart(8, '0')}`);
      if (out) { fs.writeFileSync(out, bin); console.log('scritto', out); }
    }
    if (png) { fs.writeFileSync(png, anchorSheet(r.av)); console.log('scritto', png); }
    return;
  }
  if (cmd === 'accessory') {
    const sub = args.shift();
    const readAcc = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));
    if (sub === 'validate') {
      if (!args.length) die('uso: accessory validate <file...>');
      let bad = 0;
      for (const f of args) {
        let a;
        try { a = readAcc(f); } catch (e) { console.log(`✗ ${f}: ${e.message}`); bad++; continue; }
        const r = validateAccessory(a);
        const want = path.basename(f).replace(/\.accessory\.json$/, '');
        if (!r.errors.length && want !== a.id) r.errors.push(`id "${a.id}" diverso dal nome del file: deve chiamarsi ${a.id}.accessory.json`);
        if (r.errors.length) bad++;
        console.log(`${r.errors.length ? '✗' : '✓'} ${f}${r.errors.length ? '' : `: ${a.w}×${a.h}, ${a.frames} fotogramm${a.frames > 1 ? 'i' : 'a'}, ancora ${a.anchor}, CACC ${packAccessory(a).length} B`}`);
        r.errors.forEach((e) => console.log('  errore: ' + e));
        r.warnings.forEach((w) => console.log('  avviso: ' + w));
      }
      process.exit(bad ? 1 : 0);
    }
    if (sub === 'pack') {
      const a = readAcc(args[0] || die('uso: accessory pack <file> [out.cacc]'));
      const r = validateAccessory(a);
      if (r.errors.length) die('accessorio non valido:\n  ' + r.errors.join('\n  '));
      const buf = packAccessory(a), back = unpackAccessory(buf);
      if (canonicalAccessory(back) !== canonicalAccessory(a)) die('andata e ritorno diversa');
      const out = args[1] || args[0].replace(/\.accessory\.json$|\.json$/, '') + '.cacc';
      fs.writeFileSync(out, buf);
      console.log(`scritto ${out}: ${buf.length} byte, crc32 ${buf.readUInt32LE(8).toString(16).padStart(8, '0')}, andata e ritorno identica`);
      return;
    }
    if (sub === 'unpack') {
      const s = serializeAccessory(unpackAccessory(fs.readFileSync(args[0] || die('uso: accessory unpack <in.cacc> [out.json]'))));
      if (args[1]) { fs.writeFileSync(args[1], s); console.log('scritto', args[1]); } else process.stdout.write(s);
      return;
    }
    if (sub === 'check') {
      const dir = flag('--avatars') || path.join(root, 'avatars');
      if (!args.length) die('uso: accessory check <file...> [--avatars dir]');
      const avs = avatarsIn(dir);
      const bare = avs.filter((av) => !av.anchors).map((av) => av.id);
      if (bare.length) console.log(`senza ancore (niente accessori): ${bare.join(', ')}`);
      let bad = 0;
      for (const f of args) {
        const a = readAcc(f), v = validateAccessory(a);
        const probs = v.errors.length ? v.errors.map((e) => 'errore: ' + e) : checkAccessory(a, avs);
        if (probs.length) bad++;
        console.log(`${probs.length ? '✗' : '✓'} ${f}: ${avs.filter((x) => x.anchors).length} avatar × 10 pose${probs.length ? `, ${probs.length} problemi` : ', nessun problema'}`);
        probs.forEach((m) => console.log('  ' + m));
      }
      process.exit(bad ? 1 : 0);
    }
    if (sub === 'preview') {
      const png = flag('--png'), dir = flag('--avatars') || path.join(root, 'avatars'), only = flag('--avatar'), pose = flag('--pose'), asText = bool('--text');
      const a = readAcc(args[0] || die('uso: accessory preview <file> --png out.png [--avatars dir] [--text --avatar id --pose p]'));
      const v = validateAccessory(a);
      if (v.errors.length) die('accessorio non valido:\n  ' + v.errors.join('\n  '));
      const avs = avatarsIn(dir).filter((x) => !only || x.id === only);
      if (png) { fs.writeFileSync(png, accessorySheet(a, avs)); console.log('scritto', png); }
      if (asText || !png) {
        for (const av of avs) for (const p of pose ? [pose] : POSES) {
          console.log(`${av.id} ${p}`);
          const frs = [0, 1, 2, 3].map((f) => renderWith(av, a, p, f));
          for (let y = 0; y < 25; y++) console.log(frs.map((fr) => fr[y]).join('  '));
        }
      }
      return;
    }
    die('uso: accessory validate|pack|unpack|check|preview …');
  }
  if (cmd === 'free') {
    const session = flag('--session'), dir = flag('--dir') || path.join(root, 'avatars'), json = bool('--json');
    const st = JSON.parse(fs.readFileSync(args[0] || die('uso: free <stato.json> [--session ID] [--dir cartella] [--json]'), 'utf8'));
    const lib = fs.readdirSync(dir).filter((f) => f.endsWith('.avatar.json')).map((f) => load(path.join(dir, f)).av);
    const u = usage(lib, st.assignments, st.sessions, session), free = freeChoices(lib, st.assignments, st.sessions, session);
    if (json) { console.log(JSON.stringify({ avatars: u, free: free.map((x) => x.id) }, null, 2)); return; }
    u.forEach((x) => console.log(`${x.id.padEnd(24)} ${x.state === 'libero' ? 'libero' : 'in uso da ' + x.usedBy.join(', ')}${x.default ? '  (predefinito)' : ''}`));
    return;
  }
  console.log(fs.readFileSync(fileURLToPath(import.meta.url), 'utf8').split('\n').slice(1).filter((l, i, a) => a.slice(0, i + 1).every((x) => x.startsWith('//'))).map((l) => l.replace(/^\/\/ ?/, '')).join('\n'));
  process.exit(cmd ? 2 : 0);
}
main().catch((e) => { console.error(e.message); process.exit(1); });
