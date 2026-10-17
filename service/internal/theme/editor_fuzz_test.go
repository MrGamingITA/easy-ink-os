package theme

import (
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// Temi ottenuti da sequenze casuali (seme fisso) delle operazioni dell'editor: li genera
// tools/webui-dev/test-editor.mjs (sezione 4) e ognuno deve essere salvabile dal servizio.
// Serve node (requisito di sviluppo, vedi docs/ISTRUZIONI-SVILUPPO.md).
func TestEditorFuzzThemesCompile(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Fatalf("node non trovato (serve per generare le sequenze dell'editor, requisito di sviluppo): %v", err)
	}
	script, _ := filepath.Abs("../../../tools/webui-dev/test-editor.mjs")
	out := filepath.Join(t.TempDir(), "fuzz.json")
	if b, err := exec.Command(node, script, "--fuzz-out", out).CombinedOutput(); err != nil {
		t.Fatalf("test-editor.mjs: %v\n%s", err, b)
	}
	b, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	var themes []json.RawMessage
	if err := json.Unmarshal(b, &themes); err != nil {
		t.Fatal(err)
	}
	if len(themes) != 200 {
		t.Fatalf("%d temi invece di 200", len(themes))
	}
	bad := 0
	for i, th := range themes {
		if _, _, err := CompileJSON(th); err != nil {
			bad++
			if bad <= 5 {
				t.Errorf("tema %d: %v", i, err)
			}
		}
	}
	if bad > 0 {
		t.Fatalf("%d temi su %d non compilano", bad, len(themes))
	}
}
