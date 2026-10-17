package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestAppPortDefaultFileAndEnv(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("CALAMAIO_CONFIG_DIR", dir)
	t.Setenv("CALAMAIO_APP_PORT", "")
	c, err := Load()
	if err != nil || c.AppPort != DefaultAppPort || DefaultAppPort != 8767 {
		t.Fatalf("predefinita: %v %d", err, c.AppPort)
	}
	os.WriteFile(filepath.Join(dir, "config.json"), []byte(`{"api_port":8765,"app_port":9767}`), 0o600)
	if c, _ = Load(); c.AppPort != 9767 || c.APIPort != 8765 {
		t.Fatalf("dal file: %+v", c)
	}
	os.WriteFile(filepath.Join(dir, "config.json"), []byte(`{"api_port":8765}`), 0o600)
	if c, _ = Load(); c.AppPort != DefaultAppPort {
		t.Fatalf("config vecchia senza app_port: %d", c.AppPort)
	}
	t.Setenv("CALAMAIO_APP_PORT", "18767")
	if c, _ = Load(); c.AppPort != 18767 {
		t.Fatalf("dalla variabile: %d", c.AppPort)
	}
}
