package accessory

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
	"reflect"
	"strings"
	"testing"
)

const repoRoot = "../../.."

func readBuiltin(t *testing.T, id string) []byte {
	t.Helper()
	b, err := fs.ReadFile(Builtin(), id+".accessory.json")
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func generic(t *testing.T, raw []byte) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(raw, &m); err != nil {
		t.Fatal(err)
	}
	return m
}

func enc(t *testing.T, m any) []byte {
	t.Helper()
	b, err := json.MarshalIndent(m, "", " ")
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// La copia inclusa nel binario deve coincidere con avatars/accessori/ (go generate ./internal/accessory).
func TestBuiltinAccessoriesMatchRepo(t *testing.T) {
	files, _ := filepath.Glob(filepath.Join(repoRoot, "avatars", "accessori", "*.accessory.json"))
	if len(files) == 0 {
		t.Skip("avatars/accessori del repository non trovata")
	}
	emb, _ := fs.Glob(Builtin(), "*.accessory.json")
	if len(emb) != len(files) {
		t.Fatalf("accessori inclusi %v, nel repository %v: esegui go generate ./internal/accessory", emb, files)
	}
	for _, f := range files {
		want, _ := os.ReadFile(f)
		got, err := fs.ReadFile(Builtin(), filepath.Base(f))
		if err != nil || !bytes.Equal(got, want) {
			t.Errorf("%s diverso dalla copia inclusa: esegui go generate ./internal/accessory", filepath.Base(f))
		}
	}
}

func TestBuiltinCatalog(t *testing.T) {
	cat := NewLibrary("").Catalog()
	if len(cat.Problems) > 0 {
		t.Fatalf("problemi: %v", cat.Problems)
	}
	var ids []string
	for _, e := range cat.Entries {
		ids = append(ids, e.ID)
		if len(e.Warnings) > 0 {
			t.Errorf("%s: avvisi %v", e.ID, e.Warnings)
		}
	}
	if !reflect.DeepEqual(ids, CatalogOrder) || !reflect.DeepEqual(cat.UnlockOrder(), CatalogOrder) {
		t.Fatalf("ordine %v, atteso %v", ids, CatalogOrder)
	}
}

func TestPackRoundTrip(t *testing.T) {
	for _, e := range NewLibrary("").Catalog().Entries {
		back, err := Unpack(e.Bin)
		if err != nil {
			t.Fatalf("%s: %v", e.ID, err)
		}
		a := *e.Accessory
		a.Author, a.Description = back.Author, back.Description
		if !reflect.DeepEqual(&a, back) {
			t.Errorf("%s: andata e ritorno diversa\n%+v\n%+v", e.ID, a, *back)
		}
		if len(e.Bin) > MaxBytes {
			t.Errorf("%s: %d byte", e.ID, len(e.Bin))
		}
	}
}

func TestUnpackRejects(t *testing.T) {
	good := NewLibrary("").Get("sciarpa").Bin
	fix := func(b []byte) []byte {
		binary.LittleEndian.PutUint32(b[8:], crc32.ChecksumIEEE(b[12:]))
		return b
	}
	mut := func(i int, v byte) []byte { b := bytes.Clone(good); b[i] = v; return fix(b) }
	cases := map[string][]byte{
		"magia":    append([]byte("XACC"), good[4:]...),
		"versione": mut(4, 2),
		"crc":      append(bytes.Clone(good[:len(good)-1]), good[len(good)-1]^1),
		"corto":    good[:len(good)-1],
		"w":        mut(12, 15),
		"frames":   mut(14, 3),
		"ancora":   mut(15, 5),
		"opzioni":  mut(18, 4),
		"pose":     mut(21, 0x04),
	}
	for name, b := range cases {
		if _, err := Unpack(b); err == nil {
			t.Errorf("%s: accettato", name)
		}
	}
}

// Casi rotti o al limite, sugli accessori del catalogo (indice, mutazione).
func mutations() []func(m map[string]any) {
	px := func(m map[string]any) []any { return m["pixels"].([]any) }
	return []func(m map[string]any){
		func(m map[string]any) {},
		func(m map[string]any) { m["w"] = 15.0 },
		func(m map[string]any) { m["h"] = 0.0 },
		func(m map[string]any) { m["frames"] = 3.0 },
		func(m map[string]any) { m["anchor"] = "piede" },
		func(m map[string]any) { m["anchor"] = 2.0 },
		func(m map[string]any) { m["origin"] = []any{40.0, 1.0} },
		func(m map[string]any) { m["origin"] = []any{1.5, 1.0} },
		func(m map[string]any) { m["origin"] = "1,1" },
		func(m map[string]any) { m["special"] = "no" },
		func(m map[string]any) { m["outline"] = nil },
		func(m map[string]any) { m["id"] = "Cappello Rosso" },
		func(m map[string]any) { m["id"] = strings.Repeat("a", 25) },
		func(m map[string]any) { m["name"] = "   " },
		func(m map[string]any) { m["name"] = "Cappellino è bellissimo!" },
		func(m map[string]any) { m["author"] = "" },
		func(m map[string]any) { m["description"] = strings.Repeat("à", 61) },
		func(m map[string]any) { m["version"] = 2.0 },
		func(m map[string]any) { m["format"] = "calamaio-avatar" },
		func(m map[string]any) { m["colore"] = "rosso"; m["1"] = true },
		func(m map[string]any) { m["hidePoses"] = []any{"run", "volo", "run", 3.0} },
		func(m map[string]any) {
			m["hidePoses"] = []any{"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy", "run"}
		},
		func(m map[string]any) { m["hidePoses"] = "run" },
		func(m map[string]any) {
			fr := px(m)[0].([]any)
			fr[0] = strings.Repeat("o", len(fr[0].(string)))
		},
		func(m map[string]any) { px(m)[0] = px(m)[0].([]any)[1:] },
		func(m map[string]any) {
			fr := px(m)[0].([]any)
			for i := range fr {
				fr[i] = strings.Repeat(".", len(fr[i].(string)))
			}
		},
		func(m map[string]any) {
			fr := px(m)[0].([]any)
			fr[1] = fr[1].(string) + "."
		},
		func(m map[string]any) { m["frames"] = 2.0; m["pixels"] = []any{px(m)[0], px(m)[0]} },
		func(m map[string]any) { m["pixels"] = nil },
		func(m map[string]any) { m["perAvatar"] = []any{} },
		func(m map[string]any) { m["perAvatar"] = map[string]any{"Gatto": map[string]any{"dx": 1.0}} },
		func(m map[string]any) { m["perAvatar"] = map[string]any{"gatto": map[string]any{}} },
		func(m map[string]any) { m["perAvatar"] = map[string]any{"gatto": 3.0} },
		func(m map[string]any) {
			m["perAvatar"] = map[string]any{"gatto": map[string]any{"dx": 30.0, "dy": 0.5, "zoom": 2.0}}
		},
		func(m map[string]any) { m["perAvatar"] = map[string]any{"gatto": map[string]any{"pixels": []any{}}} },
		func(m map[string]any) {
			m["perAvatar"] = map[string]any{"zeta": map[string]any{"dy": -2.0, "pixels": px(m)}, "alfa": map[string]any{"dx": 0.0}, "7": map[string]any{"dx": 1.0}}
		},
		func(m map[string]any) {
			// troppi ritocchi con pixels: oltre 1024 byte
			pa := map[string]any{}
			for i := 0; i < 12; i++ {
				pa[fmt.Sprintf("avatar-%02d", i)] = map[string]any{"pixels": px(m)}
			}
			m["perAvatar"] = pa
		},
	}
}

// Parità con Node: stessi byte di `accessory pack` sul catalogo e su accessori mutati validi; stessi
// errori e avvisi di validateAccessory() su tutti i casi.
func TestNodeParity(t *testing.T) {
	node, err := exec.LookPath("node")
	tool := filepath.Join(repoRoot, "tools", "avatar", "avatar.mjs")
	if _, serr := os.Stat(tool); err != nil || serr != nil {
		t.Skip("node o tools/avatar non disponibili")
	}
	dir := t.TempDir()
	var files []string
	write := func(name string, b []byte) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, b, 0o644); err != nil {
			t.Fatal(err)
		}
		files = append(files, p)
		return p
	}
	for _, id := range CatalogOrder {
		write(id+".accessory.json", readBuiltin(t, id))
		for i, mut := range mutations() {
			m := generic(t, readBuiltin(t, id))
			mut(m)
			write(fmt.Sprintf("caso-%s-%02d.json", id, i), enc(t, m))
		}
	}
	write("array.json", []byte("[1]"))
	write("vuoto.json", []byte("{}"))
	script := `import { validateAccessory } from ` + jsString(abs(t, filepath.Join(repoRoot, "tools/avatar/lib/accessory.mjs"))) + `;
import fs from 'node:fs';
process.stdout.write(JSON.stringify(process.argv.slice(1).map((f) => {
  const r = validateAccessory(JSON.parse(fs.readFileSync(f, 'utf8')));
  return { errors: r.errors, warnings: r.warnings };
})));`
	b, err := exec.Command(node, append([]string{"--input-type=module", "-e", script}, files...)...).Output()
	if err != nil {
		t.Fatalf("node validate: %v", err)
	}
	var want []struct{ Errors, Warnings []string }
	if err := json.Unmarshal(b, &want); err != nil {
		t.Fatal(err)
	}
	valid, invalid := 0, 0
	for i, f := range files {
		raw, _ := os.ReadFile(f)
		got := Validate(raw)
		if strings.Join(got.Errors, "\n") != strings.Join(want[i].Errors, "\n") ||
			strings.Join(got.Warnings, "\n") != strings.Join(want[i].Warnings, "\n") {
			t.Errorf("%s:\nGo   errori %q avvisi %q\nNode errori %q avvisi %q", filepath.Base(f),
				got.Errors, got.Warnings, want[i].Errors, want[i].Warnings)
			continue
		}
		if !got.OK() {
			invalid++
			continue
		}
		valid++
		// pack: stessi byte di `node avatar.mjs accessory pack`
		out := filepath.Join(dir, fmt.Sprintf("out-%d.cacc", i))
		if b, err := exec.Command(node, tool, "accessory", "pack", f, out).CombinedOutput(); err != nil {
			t.Fatalf("node accessory pack %s: %v\n%s", filepath.Base(f), err, b)
		}
		wantBin, _ := os.ReadFile(out)
		gotBin, err := Pack(got.Accessory)
		if err != nil || !bytes.Equal(gotBin, wantBin) {
			t.Errorf("%s: CACC Go diverso da Node (%d/%d byte, %v)", filepath.Base(f), len(gotBin), len(wantBin), err)
		}
	}
	if valid < len(CatalogOrder) || invalid < 20 {
		t.Fatalf("casi validi %d, non validi %d: troppo pochi per dire qualcosa", valid, invalid)
	}
	t.Logf("%d file confrontati con validateAccessory(), %d CACC identici a node", len(files), valid)
}

func jsString(s string) string { b, _ := json.Marshal(s); return string(b) }

func abs(t *testing.T, p string) string {
	a, err := filepath.Abs(p)
	if err != nil {
		t.Fatal(err)
	}
	return a
}

func TestLibraryUserAdd(t *testing.T) {
	dir := t.TempDir()
	lib := NewLibrary(dir)
	m := generic(t, readBuiltin(t, "corona"))
	if _, _, err := lib.Add(enc(t, m), false); err != ErrReserved {
		t.Fatalf("id riservato: %v", err)
	}
	m["id"], m["name"], m["special"] = "corona-d-oro", "Corona d'oro", true
	e, _, err := lib.Add(enc(t, m), false)
	if err != nil || e.Source != SourceUser || !e.Special {
		t.Fatalf("add: %v %+v", err, e)
	}
	if _, _, err := lib.Add(enc(t, m), false); err != ErrExists {
		t.Fatalf("doppio: %v", err)
	}
	if _, _, err := lib.Add(enc(t, m), true); err != nil {
		t.Fatalf("replace: %v", err)
	}
	cat := lib.Catalog()
	if cat.Get("corona-d-oro") == nil || len(cat.Entries) != 9 {
		t.Fatalf("catalogo: %d", len(cat.Entries))
	}
	// lo speciale non entra nell'ordine di sblocco; uno normale sì, dopo i predefiniti
	if got := cat.UnlockOrder(); len(got) != 8 {
		t.Fatalf("ordine di sblocco %v", got)
	}
	m["id"], m["special"] = "berretto", false
	if _, _, err := lib.Add(enc(t, m), false); err != nil {
		t.Fatal(err)
	}
	lib.Invalidate()
	if got := lib.Catalog().UnlockOrder(); got[len(got)-1] != "berretto" {
		t.Fatalf("ordine di sblocco %v", got)
	}
	// file con id diverso dal nome: problema, non caricato
	_ = os.WriteFile(filepath.Join(dir, "altro.accessory.json"), enc(t, m), 0o644)
	lib.Invalidate()
	if c := lib.Catalog(); len(c.Problems) != 1 || c.Get("altro") != nil {
		t.Fatalf("problemi %v", c.Problems)
	}
	// accessorio rotto
	if _, r, err := lib.Add([]byte(`{"format":"calamaio-accessory"}`), false); err != ErrInvalid || len(r.Errors) == 0 {
		t.Fatalf("rotto: %v %v", err, r.Errors)
	}
}
