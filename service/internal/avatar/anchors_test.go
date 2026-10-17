package avatar

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"hash/crc32"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// La copia delle ancore inclusa nel binario deve coincidere con avatars/ancore/ (go generate ./internal/avatar).
func TestBuiltinAnchorsMatchRepo(t *testing.T) {
	files, _ := filepath.Glob(filepath.Join(repoRoot, "avatars", "ancore", "*.anchors.json"))
	if len(files) == 0 {
		t.Skip("avatars/ancore del repository non trovata")
	}
	emb, _ := fs.Glob(Builtin(), "ancore/*.anchors.json")
	if len(emb) != len(files) {
		t.Fatalf("ancore incluse %v, nel repository %v: esegui go generate ./internal/avatar", emb, files)
	}
	for _, f := range files {
		want, _ := os.ReadFile(f)
		got, err := fs.ReadFile(Builtin(), "ancore/"+filepath.Base(f))
		if err != nil || !bytes.Equal(got, want) {
			t.Errorf("%s diverso dalla copia inclusa: esegui go generate ./internal/avatar", filepath.Base(f))
		}
	}
}

// I 4 predefiniti portano le ancore dal file accanto: blocco CANC di 420 byte legato al loro CAVT.
func TestBuiltinHaveAnchors(t *testing.T) {
	cat := NewLibrary("").Catalog()
	if len(cat.Problems) > 0 {
		t.Fatalf("problemi: %v", cat.Problems)
	}
	for _, id := range libIDs {
		e := cat.Get(id)
		if e == nil || e.Anchors == nil {
			t.Fatalf("%s senza ancore", id)
		}
		if len(e.Anchors) != 420 || e.AnchorsFrom != "ancore/"+id+".anchors.json" {
			t.Errorf("%s: CANC di %d byte da %q", id, len(e.Anchors), e.AnchorsFrom)
		}
		u, err := UnpackAnchors(e.Anchors)
		if err != nil {
			t.Fatal(err)
		}
		if u.AvatarCRC != e.CRC || u.Frames != 4 || len(u.Anchors) != 10 {
			t.Errorf("%s: CANC per %08x (CAVT %08x), %d fotogrammi, %d pose", id, u.AvatarCRC, e.CRC, u.Frames, len(u.Anchors))
		}
		// andata e ritorno
		for p, frs := range e.Avatar.Anchors {
			for f := range frs {
				if frs[f] != u.Anchors[p][f] {
					t.Fatalf("%s %s[%d]: %v ≠ %v", id, p, f, frs[f], u.Anchors[p][f])
				}
			}
		}
	}
	// Un punto letto con il ripiego della posa e f mod frames, come getAnchor() di Node.
	g := cat.Get("goccia").Avatar
	if pt, ok := g.Anchor("working", 6, "eyes"); !ok || pt.X != 12 || pt.Y != 14 {
		t.Errorf("goccia working[6].eyes = %v %v, atteso (12,14) come working[2]", pt, ok)
	}
	if _, ok := g.Anchor("working", 0, "hand"); ok {
		t.Error("goccia working[0].hand dovrebbe essere assente")
	}
}

// Un file accanto fatto per un altro disegno (CRC diverso) rende l'avatar non valido: mai ancore sbagliate.
func TestSidecarWrongCRC(t *testing.T) {
	raw := readLib(t, "gatto")
	r := Validate(raw)
	sc, _ := fs.ReadFile(Builtin(), "ancore/gatto.anchors.json")
	if _, errs := sidecarAnchors(r.Avatar, sc, 0x12345678); len(errs) != 1 || !strings.Contains(errs[0], "fatte per un altro disegno") {
		t.Fatalf("errori %q", errs)
	}
}

func TestUnpackAnchorsRejects(t *testing.T) {
	e := NewLibrary("").Get("calamaro")
	good := e.Anchors
	fix := func(b []byte) []byte {
		binary.LittleEndian.PutUint32(b[8:], crc32IEEE(b[12:]))
		return b
	}
	cases := map[string][]byte{
		"magia":     append([]byte("XANC"), good[4:]...),
		"versione":  fix(append(append([]byte{}, good[:4]...), append([]byte{2}, good[5:]...)...)),
		"crc":       append(append([]byte{}, good[:len(good)-1]...), good[len(good)-1]^1),
		"lunghezza": good[:len(good)-2],
		"fuori":     fix(func() []byte { b := bytes.Clone(good); b[20] = 28; return b }()),
		"fotogr.":   fix(func() []byte { b := bytes.Clone(good); b[18] = 3; return b }()),
	}
	for name, b := range cases {
		if _, err := UnpackAnchors(b); err == nil {
			t.Errorf("%s: accettato", name)
		}
	}
}

// Parità con Node: stesso blocco CANC byte per byte (`anchors --pack`) e stessi errori di validate()
// per avatar con il campo anchors (giusti e rotti).
func TestAnchorsNodeParity(t *testing.T) {
	node, err := exec.LookPath("node")
	tool := filepath.Join(repoRoot, "tools", "avatar", "avatar.mjs")
	if _, serr := os.Stat(tool); err != nil || serr != nil {
		t.Skip("node o tools/avatar non disponibili")
	}
	dir := t.TempDir()
	lib := NewLibrary("")
	for _, id := range libIDs {
		out := filepath.Join(dir, id+".canc")
		if b, err := exec.Command(node, tool, "anchors", libFile(id), "--pack", out).CombinedOutput(); err != nil {
			t.Fatalf("node anchors %s: %v\n%s", id, err, b)
		}
		want, _ := os.ReadFile(out)
		if got := lib.Get(id).Anchors; !bytes.Equal(got, want) {
			t.Fatalf("%s: CANC Go diverso da Node (%d/%d byte)", id, len(got), len(want))
		}
	}
	// validate con le ancore nel JSON: si fondono quelle del file accanto e poi si rompono.
	withAnchors := func(id string) map[string]any {
		m := generic(t, readLib(t, id))
		sc, _ := fs.ReadFile(Builtin(), "ancore/"+id+".anchors.json")
		m["anchors"] = generic(t, sc)["anchors"]
		return m
	}
	anc := func(m map[string]any) map[string]any { return m["anchors"].(map[string]any) }
	pt := func(m map[string]any, p string, f int) map[string]any {
		return anc(m)[p].([]any)[f].(map[string]any)
	}
	muts := []func(m map[string]any){
		func(m map[string]any) {},
		func(m map[string]any) { m["anchors"] = nil },
		func(m map[string]any) { m["anchors"] = []any{} },
		func(m map[string]any) { delete(anc(m), "idle") },
		func(m map[string]any) { anc(m)["volo"] = []any{} },
		func(m map[string]any) { anc(m)["done"] = anc(m)["done"].([]any)[:2] },
		func(m map[string]any) { pt(m, "error", 1)["eyes"] = []any{28.0, 3.0} },
		func(m map[string]any) { pt(m, "error", 1)["eyes"] = []any{2.5, 3.0} },
		func(m map[string]any) { pt(m, "error", 1)["eyes"] = []any{2.0} },
		func(m map[string]any) { delete(pt(m, "input", 3), "neck") },
		func(m map[string]any) { pt(m, "input", 3)["coda"] = nil },
		func(m map[string]any) { anc(m)["working"].([]any)[0] = "x" },
		func(m map[string]any) { anc(m)["working"] = map[string]any{} },
	}
	var files []string
	for i, mut := range muts {
		for _, id := range []string{"gatto", "demone"} {
			m := withAnchors(id)
			mut(m)
			p := filepath.Join(dir, fmt.Sprintf("ancore-%02d-%s.json", i, id))
			if err := os.WriteFile(p, enc(t, m), 0o644); err != nil {
				t.Fatal(err)
			}
			files = append(files, p)
		}
	}
	script := `import { validate } from ` + jsString(mustAbs(t, filepath.Join(repoRoot, "tools/avatar/lib/format.mjs"))) + `;
import fs from 'node:fs';
const out = process.argv.slice(1).map((f) => {
  const raw = fs.readFileSync(f);
  const r = validate(JSON.parse(raw.toString('utf8')), { bytes: raw.length });
  return { errors: r.errors, warnings: r.warnings };
});
process.stdout.write(JSON.stringify(out));`
	b, err := exec.Command(node, append([]string{"--input-type=module", "-e", script}, files...)...).Output()
	if err != nil {
		t.Fatalf("node validate: %v", err)
	}
	var want []struct{ Errors, Warnings []string }
	if err := json.Unmarshal(b, &want); err != nil {
		t.Fatal(err)
	}
	for i, f := range files {
		raw, _ := os.ReadFile(f)
		got := Validate(raw)
		if strings.Join(got.Errors, "\n") != strings.Join(want[i].Errors, "\n") ||
			strings.Join(got.Warnings, "\n") != strings.Join(want[i].Warnings, "\n") {
			t.Errorf("%s:\nGo   errori %q\nNode errori %q", filepath.Base(f), got.Errors, want[i].Errors)
		}
		if i < 2 && (!got.OK() || got.Avatar.Anchors == nil) {
			t.Errorf("%s: ancore nel JSON non lette", filepath.Base(f))
		}
	}
	// Un avatar dell'utente con le ancore nel JSON ha il suo blocco CANC.
	m := withAnchors("gatto")
	m["id"] = "gatto-mio"
	e, r := load(enc(t, m), SourceUser, "")
	if e == nil || e.AnchorsFrom != "json" || len(e.Anchors) != 420 {
		t.Fatalf("avatar con ancore nel JSON: %v %v", r.Errors, e)
	}
	t.Logf("%d file confrontati con validate() di Node", len(files))
}

func crc32IEEE(b []byte) uint32 { return crc32.ChecksumIEEE(b) }
