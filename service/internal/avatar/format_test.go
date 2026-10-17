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
	"regexp"
	"strings"
	"testing"
)

// Fixture: gli avatar di avatars/ e i casi di tools/avatar/test/avatar.test.mjs.
const repoRoot = "../../.."

var libIDs = []string{"goccia", "gatto", "demone", "calamaro"}

func libFile(id string) string { return filepath.Join(repoRoot, "avatars", id+".avatar.json") }

func readLib(t *testing.T, id string) []byte {
	t.Helper()
	b, err := fs.ReadFile(Builtin(), id+".avatar.json")
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

func poses(m map[string]any) map[string]any { return m["poses"].(map[string]any) }
func frame(m map[string]any, p string, f int) []any {
	return poses(m)[p].([]any)[f].([]any)
}

// La copia inclusa nel binario deve coincidere con avatars/ del repository (go generate ./internal/avatar).
func TestBuiltinMatchesRepo(t *testing.T) {
	files, _ := filepath.Glob(filepath.Join(repoRoot, "avatars", "*.avatar.json"))
	if len(files) == 0 {
		t.Skip("avatars/ del repository non trovata")
	}
	emb, _ := fs.Glob(Builtin(), "*.avatar.json")
	if len(emb) != len(files) {
		t.Fatalf("predefiniti inclusi %v, nel repository %v: esegui go generate ./internal/avatar", emb, files)
	}
	for _, f := range files {
		want, _ := os.ReadFile(f)
		got, err := fs.ReadFile(Builtin(), filepath.Base(f))
		if err != nil || !bytes.Equal(got, want) {
			t.Errorf("%s diverso dalla copia inclusa: esegui go generate ./internal/avatar", filepath.Base(f))
		}
	}
}

func TestLibraryValidWithoutWarnings(t *testing.T) {
	for _, id := range libIDs {
		r := Validate(readLib(t, id))
		if !r.OK() || len(r.Warnings) > 0 {
			t.Fatalf("%s: errori %v avvisi %v", id, r.Errors, r.Warnings)
		}
		if r.Avatar.ID != id || r.Stats.Poses != 10 {
			t.Fatalf("%s: id %q, pose %d", id, r.Avatar.ID, r.Stats.Poses)
		}
		bin, err := Pack(r.Avatar)
		if err != nil || len(bin) != 72+40*175 || len(bin) > MaxBinBytes {
			t.Fatalf("%s: binario %d byte, %v", id, len(bin), err)
		}
	}
	// stats della Goccia come docs/AVATAR.md §3 (22 px tra permission e input, 195 px d'inchiostro)
	r := Validate(readLib(t, "goccia"))
	if r.Stats.MinPoseDiff != 22 || r.Stats.Closest != "permission/input" || r.Stats.MinInk != 195 {
		t.Fatalf("stats Goccia: %+v", r.Stats)
	}
}

// Prova di presenza: il fotogramma ha inchiostro, carta e trasparenza.
func TestPresence(t *testing.T) {
	r := Validate(readLib(t, "goccia"))
	fr := strings.Join(r.Avatar.Poses["permission"][1], "")
	for _, ch := range "#+." {
		if !strings.ContainsRune(fr, ch) {
			t.Fatalf("manca %c", ch)
		}
	}
}

type mutation struct {
	name string
	base string
	mut  func(m map[string]any)
	re   string // errore atteso
}

func structuralCases() []mutation {
	return []mutation{
		{"format", "gatto", func(a map[string]any) { a["format"] = "altro" }, `format`},
		{"version", "gatto", func(a map[string]any) { a["version"] = 2 }, `version`},
		{"id", "gatto", func(a map[string]any) { a["id"] = "Gatto_Nero" }, `id`},
		{"id lungo", "gatto", func(a map[string]any) { a["id"] = strings.Repeat("a", 25) }, `id`},
		{"name lungo", "gatto", func(a map[string]any) { a["name"] = strings.Repeat("è", 13) }, `name`},
		{"description", "gatto", func(a map[string]any) { a["description"] = strings.Repeat("x", 121) }, `description`},
		{"w", "gatto", func(a map[string]any) { a["w"] = 27 }, `w deve`},
		{"frames", "gatto", func(a map[string]any) { a["frames"] = 3 }, `frames`},
		{"campo extra", "gatto", func(a map[string]any) { a["colore"] = "rosso" }, `campo sconosciuto`},
		{"posa ignota", "gatto", func(a map[string]any) { poses(a)["dance"] = poses(a)["idle"] }, `posa sconosciuta`},
		{"posa mancante", "gatto", func(a map[string]any) { delete(poses(a), "error") }, `obbligatoria mancante: error`},
		{"fotogrammi", "gatto", func(a map[string]any) { poses(a)["done"] = poses(a)["done"].([]any)[:2] }, `servono 4 fotogrammi`},
		{"righe", "gatto", func(a map[string]any) {
			p := poses(a)["idle"].([]any)
			p[0] = p[0].([]any)[1:]
		}, `servono 25 righe`},
		{"colonne", "gatto", func(a map[string]any) { f := frame(a, "idle", 0); f[3] = f[3].(string) + "." }, `servono 28 simboli`},
		{"simboli", "gatto", func(a map[string]any) { f := frame(a, "idle", 0); f[3] = strings.Repeat("x", W) }, `simboli ammessi`},
		{"posa quasi uguale", "gatto", func(a map[string]any) {
			idle := poses(a)["idle"].([]any)
			cp := make([]any, len(idle))
			for i, fr := range idle {
				cp[i] = append([]any(nil), fr.([]any)...)
			}
			r0 := cp[0].([]any)
			r0[0] = "#" + r0[0].(string)[1:]
			poses(a)["tired"] = cp
		}, `idle e tired differiscono di 1 pixel`},
		{"silhouette vuota", "gatto", func(a map[string]any) {
			empty := make([]any, H)
			for i := range empty {
				empty[i] = strings.Repeat(".", W)
			}
			poses(a)["stuffy"].([]any)[2] = empty
		}, `stuffy\[2\]: silhouette vuota`},
	}
}

func TestStructuralErrors(t *testing.T) {
	for _, c := range structuralCases() {
		m := generic(t, readLib(t, c.base))
		c.mut(m)
		r := Validate(enc(t, m))
		re := regexp.MustCompile(c.re)
		found := false
		for _, e := range r.Errors {
			if re.MatchString(e) {
				found = true
			}
		}
		if !found || r.Avatar != nil {
			t.Errorf("%s: errori %v", c.name, r.Errors)
		}
	}
}

func TestJSONTooBig(t *testing.T) {
	raw := readLib(t, "gatto")
	big := append(append([]byte{}, raw[:len(raw)-2]...), []byte(strings.Repeat(" ", 49*1024-len(raw))+"}\n")...)
	r := Validate(big)
	if r.OK() || !strings.Contains(strings.Join(r.Errors, "|"), "file JSON di") {
		t.Fatalf("peso: %v", r.Errors)
	}
	if r := Validate([]byte("{nope")); r.OK() || !strings.HasPrefix(r.Errors[0], "JSON non valido") {
		t.Fatalf("JSON rotto: %v", r.Errors)
	}
	if r := Validate([]byte("[1,2]")); r.OK() || r.Errors[0] != "il file non contiene un oggetto JSON" {
		t.Fatalf("array: %v", r.Errors)
	}
}

func warningCases() []mutation {
	return []mutation{
		{"retino, 0-1 uguali, nuvoletta", "calamaro", func(a map[string]any) {
			f := frame(a, "idle", 0)
			for y := 19; y < H; y++ {
				b := []byte(f[y].(string))
				for x := range b {
					if (x+y)%2 == 1 {
						b[x] = '#'
					} else {
						b[x] = '+'
					}
				}
				f[y] = string(b)
			}
			in := poses(a)["input"].([]any)
			in[1] = append([]any(nil), in[0].([]any)...)
			for _, fr := range poses(a)["working"].([]any) {
				rows := fr.([]any)
				for y := 0; y <= 8; y++ {
					s := rows[y].(string)
					tail := strings.NewReplacer("#", ".", "+", ".").Replace(s[17:])
					rows[y] = strings.Repeat(".", 17) + tail
				}
			}
		}, ""},
		{"corpo nella nuvoletta", "goccia", func(a map[string]any) {
			f := frame(a, "permission", 1)
			s := f[8].(string)
			f[8] = s[:20] + "#" + s[21:]
		}, ""},
		{"run: bordo spostato, piedi staccati, corsa ferma", "demone", func(a map[string]any) {
			for f := 0; f < 4; f++ {
				fr := frame(a, "run", f)
				for y := range fr {
					row := fr[y].(string)
					fr[y] = row[1:] + "." // tutto a sinistra di 1: il bordo destro lascia la colonna 27
				}
				fr[H-1] = strings.Repeat(".", W)
			}
			frame(a, "run", 1)[0] = frame(a, "run", 0)[0]
			p := a["poses"].(map[string]any)
			p["run"].([]any)[2] = p["run"].([]any)[1]
		}, ""},
		{"run: sagoma che pulsa", "goccia", func(a map[string]any) {
			fr := frame(a, "run", 2)
			for y := 0; y < 12; y++ {
				fr[y] = strings.Repeat(".", W)
			}
		}, ""},
	}
}

func TestWarnings(t *testing.T) {
	c := warningCases()
	m := generic(t, readLib(t, c[0].base))
	c[0].mut(m)
	w := strings.Join(ValidateNoSize(enc(t, m)).Warnings, "\n")
	for _, re := range []string{`idle\[0\]: \d+ blocchi a scacchiera`, `input: i fotogrammi 0 e 1 sono uguali`, `working: angolo in alto a destra vuoto`} {
		if !regexp.MustCompile(re).MatchString(w) {
			t.Errorf("manca %q in\n%s", re, w)
		}
	}
	m = generic(t, readLib(t, c[1].base))
	c[1].mut(m)
	w = strings.Join(ValidateNoSize(enc(t, m)).Warnings, "\n")
	if !strings.Contains(w, "permission[1]: 1 pixel nell'angolo della nuvoletta fuori dal suo ingombro (20,8)") {
		t.Errorf("nuvoletta: %s", w)
	}
}

func minimal(t *testing.T) []byte {
	m := generic(t, readLib(t, "demone"))
	m["frames"] = 2
	for _, p := range Poses {
		if contains(Required, p) {
			poses(m)[p] = poses(m)[p].([]any)[:2]
		} else {
			delete(poses(m), p)
		}
	}
	return enc(t, m)
}

func TestMinimalAvatar(t *testing.T) {
	r := Validate(minimal(t))
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	bin, err := Pack(r.Avatar)
	if err != nil || len(bin) != PackedSize(6, 2, W, H) || len(bin) != 72+6*2*175 {
		t.Fatalf("%d %v", len(bin), err)
	}
	back, err := Unpack(bin)
	if err != nil || strings.Join(back.Present(), ",") != strings.Join(Required, ",") {
		t.Fatalf("%v %v", back, err)
	}
	if back.PoseFrames("tired")[0][0] != back.Poses["idle"][0][0] || back.PoseFrames("compact") == nil {
		t.Fatal("ripiego delle pose")
	}
}

func TestPackRoundTripAndHeader(t *testing.T) {
	for _, id := range libIDs {
		r := Validate(readLib(t, id))
		bin, _ := Pack(r.Avatar)
		back, err := Unpack(bin)
		if err != nil || back.ID != id || back.Name != r.Avatar.Name || back.Frames != 4 {
			t.Fatalf("%s: %v", id, err)
		}
		for _, p := range Poses {
			if strings.Join(flat(back.Poses[p]), "") != strings.Join(flat(r.Avatar.Poses[p]), "") {
				t.Fatalf("%s %s diversa", id, p)
			}
		}
	}
	m := generic(t, readLib(t, "goccia"))
	f := frame(m, "working", 0)
	f[0] = "#+." + f[0].(string)[3:]
	r := ValidateNoSize(enc(t, m))
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	b, _ := Pack(r.Avatar)
	if string(b[:4]) != "CAVT" || b[4] != 1 || b[5] != 72 || int(binary.LittleEndian.Uint16(b[6:])) != len(b) ||
		b[12] != 28 || b[13] != 25 || b[14] != 4 || binary.LittleEndian.Uint16(b[16:]) != 0x3FF ||
		binary.LittleEndian.Uint16(b[18:]) != 175 || CRC(b) != crc32.ChecksumIEEE(b[12:]) {
		t.Fatalf("intestazione: % x", b[:72])
	}
	if b[72]>>2 != 0b011000 {
		t.Fatalf("pixel: %08b", b[72])
	}
}

func flat(frs [][]string) []string {
	var out []string
	for _, f := range frs {
		out = append(out, f...)
	}
	return out
}

func TestUnpackRejects(t *testing.T) {
	r := Validate(readLib(t, "gatto"))
	buf, _ := Pack(r.Avatar)
	cp := func() []byte { return append([]byte(nil), buf...) }
	flip := cp()
	flip[500] ^= 1
	magic := cp()
	magic[0] = 0
	ver := cp()
	ver[4] = 2
	three := cp()
	three[72] |= 0xC0
	binary.LittleEndian.PutUint32(three[8:], crc32.ChecksumIEEE(three[12:]))
	for name, c := range map[string]struct {
		b  []byte
		re string
	}{"crc": {flip, "checksum"}, "corto": {buf[:len(buf)-1], "lunghezza"}, "magia": {magic, "CAVT"},
		"versione": {ver, "versione"}, "codice 3": {three, "riservato"}} {
		if _, err := Unpack(c.b); err == nil || !strings.Contains(err.Error(), c.re) {
			t.Errorf("%s: %v", name, err)
		}
	}
}

func TestCRCVector(t *testing.T) {
	if crc32.ChecksumIEEE([]byte("123456789")) != 0xCBF43926 {
		t.Fatal("crc32")
	}
}

// CRC dei 4 predefiniti misurati con `node tools/avatar/avatar.mjs pack` (docs/AVATAR.md §12.3): con la
// posa run (7 072 byte) e senza (6 372 byte, il CAVT di prima, per il firmware ≤ 0.6.0). Restano
// verificati anche dove node non c'è.
var (
	knownCRC       = map[string]uint32{"goccia": 0x33072cee, "gatto": 0xb2432ad7, "demone": 0xa3130345, "calamaro": 0x2d86bcb3}
	knownLegacyCRC = map[string]uint32{"goccia": 0x0386cc22, "gatto": 0xd6aebfcb, "demone": 0xc6e4fa9b, "calamaro": 0xc2fb3686}
)

func TestKnownCRC(t *testing.T) {
	for id, want := range knownCRC {
		a := Validate(readLib(t, id)).Avatar
		bin, _ := Pack(a)
		if CRC(bin) != want || len(bin) != 7072 {
			t.Errorf("%s: crc32 %08x (%d byte), misurato con node %08x (avatar cambiato? aggiorna knownCRC)", id, CRC(bin), len(bin), want)
		}
		old, _ := PackLegacy(a)
		if CRC(old) != knownLegacyCRC[id] || len(old) != 6372 || binary.LittleEndian.Uint16(old[16:]) != 0x1FF {
			t.Errorf("%s: senza run crc32 %08x (%d byte), atteso %08x (6 372 byte, mask 0x1FF)", id, CRC(old), len(old), knownLegacyCRC[id])
		}
		// Senza run il binario è un prefisso compatibile: stesse pose 0-8 negli stessi byte.
		if !bytes.Equal(old[Header:], bin[Header:len(old)]) {
			t.Errorf("%s: le pose 0-8 non hanno gli stessi byte con e senza run", id)
		}
		// Il CAVT senza run si rilegge (Unpack) e la posa run ripiega su working.
		back, err := Unpack(old)
		if err != nil || back.Poses["run"] != nil || len(back.PoseFrames("run")) != len(a.Poses["working"]) {
			t.Errorf("%s: CAVT senza run: %v", id, err)
		}
	}
}

// Un avatar senza run: PackLegacy è identico a Pack.
func TestPackLegacyWithoutRun(t *testing.T) {
	m := generic(t, readLib(t, "gatto"))
	delete(m["poses"].(map[string]any), "run")
	r := ValidateNoSize(enc(t, m))
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	a, _ := Pack(r.Avatar)
	b, _ := PackLegacy(r.Avatar)
	if !bytes.Equal(a, b) || CRC(a) != knownLegacyCRC["gatto"] {
		t.Fatalf("senza run: %08x / %08x", CRC(a), CRC(b))
	}
}

// Confronto diretto con gli strumenti Node: stesso binario byte per byte e stessi errori e avvisi
// (stesso testo, stesso ordine) su libreria e casi mutati.
func TestNodeParity(t *testing.T) {
	node, err := exec.LookPath("node")
	tool := filepath.Join(repoRoot, "tools", "avatar", "avatar.mjs")
	if _, serr := os.Stat(tool); err != nil || serr != nil {
		t.Skip("node o tools/avatar non disponibili")
	}
	dir := t.TempDir()
	// pack
	for _, id := range libIDs {
		out := filepath.Join(dir, id+".cavt")
		cmd := exec.Command(node, tool, "pack", libFile(id), out)
		if b, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("node pack %s: %v\n%s", id, err, b)
		}
		want, _ := os.ReadFile(out)
		r := Validate(readLib(t, id))
		got, _ := Pack(r.Avatar)
		if !bytes.Equal(got, want) {
			t.Fatalf("%s: binario Go diverso da Node (%d/%d byte)", id, len(got), len(want))
		}
		t.Logf("%s: %d byte, crc32 %08x, identico a node", id, len(got), CRC(got))
	}
	// validate: libreria, casi strutturali, avvisi, minimo, rotti
	var files []string
	write := func(name string, b []byte) {
		p := filepath.Join(dir, name+".json")
		if err := os.WriteFile(p, b, 0o644); err != nil {
			t.Fatal(err)
		}
		files = append(files, p)
	}
	for _, id := range libIDs {
		write("lib-"+id, readLib(t, id))
	}
	for i, c := range append(structuralCases(), warningCases()...) {
		m := generic(t, readLib(t, c.base))
		c.mut(m)
		write(fmt.Sprintf("caso-%02d", i), enc(t, m))
	}
	write("minimo", minimal(t))
	write("array", []byte("[1]"))
	write("null-pose", []byte(`{"format":"calamaio-avatar","version":1,"id":"x","name":"X","author":"a","description":"","w":28,"h":25,"frames":2,"poses":{"working":null,"compact":null}}`))
	write("tipi", []byte(`{"format":1,"version":"1","id":5,"name":"  ","author":"","w":28.0,"h":"25","frames":4,"poses":[]}`))
	big := readLib(t, "gatto")
	write("pesante", append(append([]byte{}, big[:len(big)-2]...), []byte(strings.Repeat(" ", 12000)+"}\n")...))
	script := `import { validate } from ` + jsString(mustAbs(t, filepath.Join(repoRoot, "tools/avatar/lib/format.mjs"))) + `;
import fs from 'node:fs';
const out = process.argv.slice(1).map((f) => {
  const raw = fs.readFileSync(f);
  const r = validate(JSON.parse(raw.toString('utf8')), { bytes: raw.length });
  return { errors: r.errors, warnings: r.warnings };
});
process.stdout.write(JSON.stringify(out));`
	cmd := exec.Command(node, append([]string{"--input-type=module", "-e", script}, files...)...)
	b, err := cmd.Output()
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
			t.Errorf("%s:\nGo   errori %q avvisi %q\nNode errori %q avvisi %q", filepath.Base(f),
				got.Errors, got.Warnings, want[i].Errors, want[i].Warnings)
		}
	}
	t.Logf("%d file confrontati con validate() di Node", len(files))
}

func jsString(s string) string { b, _ := json.Marshal(s); return string(b) }

func mustAbs(t *testing.T, p string) string {
	a, err := filepath.Abs(p)
	if err != nil {
		t.Fatal(err)
	}
	return a
}
