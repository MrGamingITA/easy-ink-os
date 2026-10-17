package main

import (
	"path/filepath"
	"testing"

	"calamaio/internal/server"
)

func TestDataPaths(t *testing.T) {
	d := t.TempDir()
	var o server.Options
	dataPaths(&o, d)
	if o.PhonesFile != filepath.Join(d, "phones.json") || o.AppTLSDir != filepath.Join(d, "app-tls") ||
		o.GameFile != filepath.Join(d, "game.json") || o.ThemeDir != filepath.Join(d, "themes") || o.CrashDir != filepath.Join(d, "crash") {
		t.Fatalf("percorsi: %+v", o)
	}
}
