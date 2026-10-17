// mkavatars copia i file predefiniti del repository (avatars/*.avatar.json, le ancore in
// avatars/ancore/, gli accessori in avatars/accessori/) dentro il servizio, che li include nel binario
// con go:embed. go:embed non può leggere fuori dal modulo service/, quindi serve una copia; i test
// TestBuiltinMatchesRepo (avatar, ancore) e TestBuiltinAccessoriesMatchRepo falliscono se diverge.
//
// Uso (con go generate): go run ../../cmd/mkavatars <cartella del repository> <copia> [modello]
// (modello predefinito *.avatar.json; es. *.anchors.json, *.accessory.json)
package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	if len(os.Args) != 3 && len(os.Args) != 4 {
		fmt.Fprintln(os.Stderr, "uso: mkavatars <cartella del repository> <copia> [modello, es. *.accessory.json]")
		os.Exit(2)
	}
	src, dst := os.Args[1], os.Args[2]
	pattern := "*.avatar.json"
	if len(os.Args) == 4 {
		pattern = os.Args[3]
	}
	suffix := strings.TrimPrefix(pattern, "*")
	files, err := filepath.Glob(filepath.Join(src, pattern))
	if err != nil || len(files) == 0 {
		fmt.Fprintf(os.Stderr, "mkavatars: nessun %s in %s (%v)\n", pattern, src, err)
		os.Exit(1)
	}
	if err := os.MkdirAll(dst, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, "mkavatars:", err)
		os.Exit(1)
	}
	keep := map[string]bool{}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			fmt.Fprintln(os.Stderr, "mkavatars:", err)
			os.Exit(1)
		}
		name := filepath.Base(f)
		keep[name] = true
		out := filepath.Join(dst, name)
		if old, err := os.ReadFile(out); err == nil && bytes.Equal(old, b) {
			continue
		}
		if err := os.WriteFile(out, b, 0o644); err != nil {
			fmt.Fprintln(os.Stderr, "mkavatars:", err)
			os.Exit(1)
		}
		fmt.Println("copiato", name)
	}
	// I file tolti dal repository spariscono anche dalla copia.
	old, _ := filepath.Glob(filepath.Join(dst, pattern))
	for _, f := range old {
		if !keep[filepath.Base(f)] && strings.HasSuffix(f, suffix) {
			_ = os.Remove(f)
			fmt.Println("tolto", filepath.Base(f))
		}
	}
}
