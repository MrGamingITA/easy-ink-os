package capture

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestWriteJSONL(t *testing.T) {
	dir := t.TempDir()
	w, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	w.now = func() time.Time { return time.Date(2026, 10, 11, 13, 0, 0, 0, time.UTC) }
	if err := w.Write("hook", []byte(`{"hook_event_name": "Stop", "session_id": "a"}`)); err != nil {
		t.Fatal(err)
	}
	if err := w.Write("statusline", []byte(`{"session_id":"a"}`)); err != nil {
		t.Fatal(err)
	}
	if err := w.Write("hook", []byte(`non json`)); err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(filepath.Join(dir, "capture-2026-10-11.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var got []Record
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		var r Record
		if err := json.Unmarshal(sc.Bytes(), &r); err != nil {
			t.Fatalf("riga non JSON: %v", err)
		}
		got = append(got, r)
	}
	if len(got) != 3 {
		t.Fatalf("righe = %d, attese 3", len(got))
	}
	if got[0].Event != "Stop" || string(got[0].Payload) != `{"hook_event_name":"Stop","session_id":"a"}` {
		t.Errorf("riga 0 = %+v", got[0])
	}
	if got[1].Event != "statusline" || got[1].Source != "statusline" {
		t.Errorf("riga 1 = %+v", got[1])
	}
	if got[2].Event != "invalid" || got[2].Raw != "non json" {
		t.Errorf("riga 2 = %+v", got[2])
	}
}
