package sessions

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Le fixture vanno nel repository: niente percorsi, nomi o account dell'utente (stesso elenco
// di cmd/mkfixtures, che si rifiuta di scriverle se ne trova uno).
func TestFixturesHaveNoPersonalData(t *testing.T) {
	files, _ := filepath.Glob(filepath.Join(fixturesDir, "*.jsonl"))
	if len(files) < 3 {
		t.Fatalf("fixture: %v", files)
	}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		low := strings.ToLower(string(b))
		words := []string{"/users/", "/private/", "claude-501", "downloads", "calamaios",
			"5e836d04", "gmail", "unreal", "mrgaming"}
		if u := strings.ToLower(os.Getenv("USER")); len(u) >= 3 { // il nome dell'utente del Mac
			words = append(words, u)
		}
		for _, w := range words {
			if strings.Contains(low, w) {
				t.Errorf("%s contiene %q", filepath.Base(f), w)
			}
		}
	}
}
