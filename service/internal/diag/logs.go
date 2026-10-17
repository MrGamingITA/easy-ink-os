package diag

import (
	"errors"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

// LogFiles salva il registro da remoto in Dir (<dati>/calamaio/logs) come <board>-<AAAA-MM-GG>.log,
// un file per scheda e giorno locale (file 0600: il registro può contenere il nome della rete Wi-Fi).
type LogFiles struct {
	Dir string
	mu  sync.Mutex
}

var dayRe = regexp.MustCompile(`^[0-9]{4}-[0-9]{2}-[0-9]{2}$`)

// Path restituisce il file di board per il giorno day (AAAA-MM-GG).
func (l *LogFiles) Path(board, day string) (string, error) {
	if !dayRe.MatchString(day) || !safeBoard(board) {
		return "", errors.New("scheda o giorno non validi")
	}
	return filepath.Join(l.Dir, board+"-"+day+".log"), nil
}

func safeBoard(b string) bool { return regexp.MustCompile(`^[a-z0-9][a-z0-9-]{0,30}$`).MatchString(b) }

// Append aggiunge righe al file del giorno di t. Ogni riga è già ripulita dai caratteri di controllo.
func (l *LogFiles) Append(board string, t time.Time, lines []string) (string, error) {
	path, err := l.Path(board, t.Local().Format("2006-01-02"))
	if err != nil {
		return "", err
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := os.MkdirAll(l.Dir, 0o700); err != nil {
		return "", err
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o600)
	if err != nil {
		return "", err
	}
	defer f.Close()
	var b strings.Builder
	for _, s := range lines {
		b.WriteString(s)
		b.WriteByte('\n')
	}
	_, err = f.WriteString(b.String())
	return path, err
}

// CleanLine toglie i caratteri di controllo (tranne il tab) e i caratteri UTF-8 non validi.
func CleanLine(s string) string {
	s = strings.ToValidUTF8(strings.TrimRight(s, "\r\n"), "?")
	return strings.Map(func(r rune) rune {
		if r == '\t' {
			return r
		}
		if r < 0x20 || r == 0x7F {
			return -1
		}
		return r
	}, s)
}
