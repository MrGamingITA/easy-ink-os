// Package capture salva i payload ricevuti in file JSONL, uno per giorno (milestone M0).
package capture

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Record è una riga del file di cattura.
type Record struct {
	TS      string          `json:"ts"`
	Source  string          `json:"source"` // "hook" oppure "statusline"
	Event   string          `json:"event"`  // hook_event_name, oppure "statusline"
	Payload json.RawMessage `json:"payload,omitempty"`
	Raw     string          `json:"raw,omitempty"` // solo se il corpo non era JSON valido
}

type Writer struct {
	dir string
	now func() time.Time
	mu  sync.Mutex
}

func New(dir string) (*Writer, error) {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	return &Writer{dir: dir, now: time.Now}, nil
}

func (w *Writer) Dir() string { return w.dir }

// Write accoda un payload. Il corpo viene salvato così com'è se è JSON valido.
func (w *Writer) Write(source string, body []byte) error {
	t := w.now()
	rec := Record{TS: t.Format(time.RFC3339Nano), Source: source}
	if json.Valid(body) {
		rec.Payload = compact(body)
		rec.Event = EventName(source, body)
	} else {
		rec.Raw = string(body)
		rec.Event = "invalid"
	}
	line, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	name := filepath.Join(w.dir, "capture-"+t.Format("2006-01-02")+".jsonl")

	w.mu.Lock()
	defer w.mu.Unlock()
	f, err := os.OpenFile(name, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(append(line, '\n'))
	return err
}

// EventName ricava il nome dell'evento da un payload.
func EventName(source string, body []byte) string {
	if source == "statusline" {
		return "statusline"
	}
	var head struct {
		Name string `json:"hook_event_name"`
	}
	if json.Unmarshal(body, &head) != nil || head.Name == "" {
		return "unknown"
	}
	return head.Name
}

func compact(b []byte) json.RawMessage {
	var buf bytes.Buffer
	if err := json.Compact(&buf, b); err != nil {
		return json.RawMessage(b)
	}
	return json.RawMessage(buf.Bytes())
}
