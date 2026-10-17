package webui

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// La copia della webapp (static/assets/js/accessory.js) disegna gli accessori pixel per pixel come il
// riferimento tools/avatar/lib/draw-accessory.mjs: catalogo × 4 avatar con le ancore × 10 pose ×
// 4 fotogrammi, più varianti (contorno acceso, ritocchi tolti, origine spostata) e un avatar senza ancore.
func TestAccessoryDrawParityWithReference(t *testing.T) {
	node, err := exec.LookPath("node")
	repo, _ := filepath.Abs("../../..")
	ref := filepath.Join(repo, "tools", "avatar", "lib", "draw-accessory.mjs")
	if _, serr := os.Stat(ref); err != nil || serr != nil {
		t.Skip("node o tools/avatar non disponibili")
	}
	web, _ := filepath.Abs("static/assets/js/accessory.js")
	script := `
import fs from 'node:fs';
import path from 'node:path';
import * as R from ` + q(ref) + `;
import { loadWithAnchors } from ` + q(filepath.Join(repo, "tools/avatar/lib/anchors.mjs")) + `;
import { POSES } from ` + q(filepath.Join(repo, "tools/avatar/lib/format.mjs")) + `;
import * as Wb from ` + q(web) + `;
const repo = ` + q(repo) + `;
const avs = fs.readdirSync(path.join(repo, 'avatars')).filter((f) => f.endsWith('.avatar.json')).map((f) => loadWithAnchors(path.join(repo, 'avatars', f)).av);
avs.push({ ...avs[0], id: 'senza-ancore', anchors: undefined });
let accs = fs.readdirSync(path.join(repo, 'avatars', 'accessori')).filter((f) => f.endsWith('.accessory.json'))
  .map((f) => JSON.parse(fs.readFileSync(path.join(repo, 'avatars', 'accessori', f), 'utf8')));
const variants = [];
for (const a of accs) {
  variants.push({ ...a, outline: !a.outline, id: a.id + '-contorno' });
  variants.push({ ...a, perAvatar: {}, id: a.id + '-senza-ritocchi' });
  variants.push({ ...a, origin: [0, 0], hidePoses: [], id: a.id + '-origine' });
}
accs = accs.concat(variants);
let n = 0, drawn = 0, diff = [];
for (const acc of accs) for (const av of avs) for (const pose of POSES) for (let f = 0; f < 4; f++) {
  const a = R.renderWith(av, acc, pose, f).join('\n');
  const b = (Wb.renderWith(av, acc, pose, f) || []).join('\n');
  const plain = R.renderWith(av, null, pose, f).join('\n');
  n++;
  if (a !== plain) drawn++;
  if (a !== b) diff.push(acc.id + ' ' + av.id + ' ' + pose + '[' + f + ']');
}
process.stdout.write(JSON.stringify({ n, drawn, diff: diff.slice(0, 10), ndiff: diff.length }));
`
	out, err := exec.Command(node, "--input-type=module", "-e", script).CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}
	s := string(out)
	if !strings.Contains(s, `"ndiff":0`) {
		t.Fatalf("disegno diverso dal riferimento: %s", s)
	}
	t.Logf("confronto: %s", s)
}

func q(s string) string { return "'" + strings.ReplaceAll(s, "'", "\\'") + "'" }
