package webui

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// noCacheStamp: file dentro il modulo che il test riscrive e poi apre, ogni volta.
//
// Perché: node gira in un sottoprocesso e legge file fuori dal modulo service/ (golden.json, scenarios.json, le
// fixture dei temi, firmware/src/gfx/fonts/f_*.h, gli script .mjs, gli avatar). La cache di go test non vede le
// letture di un sottoprocesso, e anche quelle fatte dal test con os.ReadFile le ignora se il file sta fuori dal
// modulo (cmd/go: «Do not recheck files outside the module»): leggerli qui non basterebbe, e un «(cached) ok» vecchio
// passerebbe. Invece la cache rifiuta un risultato se il test ha aperto un file del modulo modificato da meno di 2 s
// (errFileTooNew): scrivere e riaprire questo file rende il pacchetto non memorizzabile, e il controllo rigira sempre
// (costa meno di un secondo). Vale anche per un ingresso nuovo che nessuno ricorda di elencare.
const noCacheStamp = ".theme-parity-nocache"

func skipTestCache(t *testing.T) {
	// Checkout in sola lettura: niente stamp, il controllo resta; si perde solo il salto della cache (risultato vecchio possibile).
	if err := os.WriteFile(noCacheStamp, []byte(time.Now().Format(time.RFC3339Nano)+"\n"), 0o644); err != nil {
		t.Logf("file per saltare la cache non scritto (il risultato può venire dalla cache): %v", err)
		return
	}
	if _, err := os.ReadFile(noCacheStamp); err != nil {
		t.Logf("file per saltare la cache non riletto (il risultato può venire dalla cache): %v", err)
	}
}

// Renderer dei temi (static/assets/js/theme): fonts.js è l'export dei .h del firmware e golden.json, che il test
// C++ test_theme_parity confronta con l'interprete della scheda, corrisponde al renderer di oggi. Insieme danno la
// parità C++ = JS (docs/TEMI.md §7): JS scrive il golden, il C++ lo rifà, qui si controlla che non sia vecchio.
func TestThemeRendererParity(t *testing.T) {
	// In Cleanup, cioè alla fine (anche dopo uno Skip): il file deve avere meno di 2 s quando go test calcola la chiave.
	t.Cleanup(func() { skipTestCache(t) })
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node non disponibile: freschezza di fonts.js e del golden dei temi non controllata")
	}
	repo, _ := filepath.Abs("../../..")
	for _, args := range [][]string{
		{filepath.Join(repo, "tools", "fonts", "export-js.mjs"), "--check"},
		{filepath.Join(repo, "tools", "golden", "theme-golden.mjs"), "--check"},
	} {
		out, err := exec.Command(node, args...).CombinedOutput()
		if err != nil {
			t.Fatalf("%s --check: %v\n%s", filepath.Base(args[0]), err, out)
		}
		t.Logf("%s", out)
	}
}
