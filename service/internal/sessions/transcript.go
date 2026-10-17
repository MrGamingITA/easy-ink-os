package sessions

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// Nessun hook segnala un permesso negato nel terminale né un'interruzione con Esc: la
// documentazione dice che PermissionDenied vale solo in modalità auto e che Stop non parte se
// l'utente interrompe. Nel transcript però Claude Code scrive un messaggio utente
// «[Request interrupted by user for tool use]» (negato) o «[Request interrupted by user]» (Esc).
// Lo si cerca solo in coda al file e solo quando serve (vedi Engine.check). Formato interno di
// Claude Code, non documentato: se cambia, il servizio torna a «working» invece di «idle».

const interruptMarker = "[Request interrupted by user"

// tailBytes: quanto si legge dalla fine del transcript. Una riga sola può essere grande
// (risultato di uno strumento): 256 KB bastano per arrivare all'ultimo messaggio utente.
const tailBytes = 256 << 10

// ReadTail legge gli ultimi tailBytes di un transcript. Accetta solo percorsi assoluti a file .jsonl.
func ReadTail(path string) ([]byte, error) {
	if !filepath.IsAbs(path) || !strings.HasSuffix(path, ".jsonl") {
		return nil, errors.New("transcript_path non valido")
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return nil, err
	}
	if !st.Mode().IsRegular() {
		return nil, errors.New("transcript non regolare")
	}
	off := st.Size() - tailBytes
	if off < 0 {
		off = 0
	}
	if _, err := f.Seek(off, io.SeekStart); err != nil {
		return nil, err
	}
	b, err := io.ReadAll(io.LimitReader(f, tailBytes))
	if err != nil {
		return nil, err
	}
	if off > 0 { // la prima riga è a metà
		if i := bytes.IndexByte(b, '\n'); i >= 0 {
			b = b[i+1:]
		}
	}
	return b, nil
}

// Interrupted dice se l'ultimo messaggio della conversazione (utente o assistente) nella coda
// del transcript è l'interruzione dell'utente. forTool = negato su una richiesta di permesso.
func Interrupted(tail []byte) (interrupted, forTool bool) {
	lines := bytes.Split(bytes.TrimSpace(tail), []byte("\n"))
	for i := len(lines) - 1; i >= 0; i-- {
		var e struct {
			Type    string `json:"type"`
			Message struct {
				Content json.RawMessage `json:"content"`
			} `json:"message"`
		}
		if json.Unmarshal(lines[i], &e) != nil {
			continue
		}
		switch e.Type {
		case "assistant":
			return false, false
		case "user":
			for _, t := range texts(e.Message.Content) {
				if strings.HasPrefix(strings.TrimSpace(t), interruptMarker) {
					return true, strings.Contains(t, "for tool use")
				}
			}
			return false, false
		}
	}
	return false, false
}

// texts: i testi di un content (stringa, oppure blocchi {type: text, text}).
func texts(c json.RawMessage) []string {
	var s string
	if json.Unmarshal(c, &s) == nil {
		return []string{s}
	}
	var blocks []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	}
	_ = json.Unmarshal(c, &blocks)
	out := []string{}
	for _, b := range blocks {
		if b.Type == "text" {
			out = append(out, b.Text)
		}
	}
	return out
}
