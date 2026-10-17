#!/usr/bin/env node
// Prova di determinismo: esegue la cattura due volte in due cartelle temporanee e confronta
// ogni file byte per byte; poi confronta la seconda esecuzione con out/heltec213 (se esiste).
// Uscita 0 solo se tutto e identico.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readdirSync, readFileSync, existsSync, rmSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const keep = process.argv.includes('--keep');
const runs = [mkdtempSync(path.join(tmpdir(), 'simcap-a-')), mkdtempSync(path.join(tmpdir(), 'simcap-b-'))];
for (const d of runs) execFileSync(process.execPath, [path.join(HERE, 'capture.mjs'), '--out', d, '--quiet'], { stdio: 'inherit' });

const sha = (f) => createHash('sha256').update(readFileSync(f)).digest('hex');
function compareDirs(a, b, label) {
  const fa = readdirSync(a).sort(), fb = readdirSync(b).sort();
  const problems = [];
  if (fa.join('|') !== fb.join('|')) problems.push('elenco file diverso');
  let same = 0;
  for (const f of fa) {
    if (!fb.includes(f)) continue;
    if (readFileSync(path.join(a, f)).equals(readFileSync(path.join(b, f)))) same++;
    else problems.push('diverso: ' + f);
  }
  console.log(`${label}: ${same}/${fa.length} file identici byte per byte` + (problems.length ? '\n  ' + problems.join('\n  ') : ''));
  return problems.length === 0;
}

let ok = compareDirs(runs[0], runs[1], 'esecuzione 1 contro esecuzione 2');
const manifest = path.join(runs[0], 'manifest.json');
console.log('sha256 manifest.json: ' + sha(manifest) + ' / ' + sha(path.join(runs[1], 'manifest.json')));
const committed = path.join(HERE, 'out', 'heltec213');
if (existsSync(committed)) ok = compareDirs(runs[1], committed, 'esecuzione 2 contro out/heltec213') && ok;
if (keep) console.log('cartelle conservate: ' + runs.join(' '));
else runs.forEach((d) => rmSync(d, { recursive: true, force: true }));
console.log(ok ? 'DETERMINISTICO' : 'NON DETERMINISTICO');
process.exit(ok ? 0 : 1);
