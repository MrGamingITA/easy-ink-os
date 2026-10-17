package sessions

import (
	"encoding/json"
	"fmt"
	"os"
	"testing"
	"time"
)

func TestDumpReplay(t *testing.T) {
	if os.Getenv("DUMP") == "" {
		t.Skip()
	}
	recs := loadFixture(t, "cattura-2026-10-11.jsonl")
	t0, _ := time.Parse(time.RFC3339Nano, recs[0].TS)
	r := newRig(t, t0.Add(-time.Second))
	r.denyTail(t)
	for i, x := range recs {
		if i == 19 {
			s := r.session(sidA)
			fmt.Printf("    prima di 19: %s %q\n", s.State, s.Detail)
		}
		r.play(x)
		var p struct {
			SessionID string `json:"session_id"`
		}
		_ = jsonUnmarshal(x.Payload, &p)
		s := r.session(p.SessionID)
		if s == nil {
			fmt.Printf("%3d %-18s (assente)\n", i, x.Event)
			continue
		}
		test := "-"
		if s.Test != nil {
			test = *s.Test
		}
		ctx := -1
		if s.Ctx != nil {
			ctx = *s.Ctx
		}
		fmt.Printf("%3d %-18s %-10s attn=%v subs=%d test=%s ctx=%d tasks=%v name=%q detail=%q sum=%q\n", i, x.Event, s.State, s.AttnAt != nil, s.Subs, test, ctx, s.Tasks, s.Name, s.Detail, s.Summary)
	}
	r.at(r.now.Add(10 * time.Second))
	sn := r.store.Snapshot()
	fmt.Printf("fine: %d sessioni, today=%+v limits=%v\n", len(sn.Sessions), sn.Today, sn.Limits != nil)
	for _, l := range sn.Log {
		fmt.Println("  log", time.UnixMilli(l.T).Format("15:04:05"), l.Name, "|", l.Text)
	}
}

func jsonUnmarshal(b []byte, v any) error { return json.Unmarshal(b, v) }
