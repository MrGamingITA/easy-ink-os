package sessions

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// Oggi si salva nella cartella dati dell'utente (brief 5.3), così un riavvio del servizio nella
// stessa giornata non azzera i contatori.

func saveToday(file string, t todayStats) error {
	if err := os.MkdirAll(filepath.Dir(file), 0o700); err != nil {
		return err
	}
	b, err := json.MarshalIndent(t, "", "  ")
	if err != nil {
		return err
	}
	tmp := file + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, file)
}

func loadToday(file string) (todayStats, error) {
	var t todayStats
	b, err := os.ReadFile(file)
	if err != nil {
		return t, err
	}
	err = json.Unmarshal(b, &t)
	return t, err
}
