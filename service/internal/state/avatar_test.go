package state

import (
	"encoding/json"
	"strings"
	"testing"
)

type stubAvatars struct{}

func (stubAvatars) SessionAvatar(id, cwd string) AvatarRef {
	return AvatarRef{ID: strings.Repeat("a", 24), CRC: 0xFFFFFFFF, From: "progetto", Project: cwd}
}
func (stubAvatars) DefaultAvatar() AvatarRef {
	return AvatarRef{ID: strings.Repeat("b", 24), CRC: 0xFFFFFFFF}
}

// Con gli avatar (id e CRC massimi su 6 sessioni e in radice) lo state resta sotto i 12 KB anche
// pieno di testi \uXXXX, e la cartella delle sessioni non va mai ai display.
func TestMessageAvatarsAndSize(t *testing.T) {
	s := NewStore(Empty())
	s.Avatars = stubAvatars{}
	long := strings.Repeat("\u0001è", 200)
	s.Update(func(sn *Snapshot) {
		for i := 0; i < 8; i++ {
			ss := Session{ID: strings.Repeat("x", 36) + string(rune('a'+i)), Name: long, State: Working, Detail: long,
				Summary: long, Cwd: "/Users/qualcuno/progetto-segreto"}
			for j := 0; j < 12; j++ {
				ss.Tasks = append(ss.Tasks, Task{Title: long})
			}
			sn.Sessions = append(sn.Sessions, ss)
		}
		for i := 0; i < 20; i++ {
			sn.AddLog(1, long, long)
		}
	})
	b, _ := json.Marshal(s.Message())
	if len(b) > MaxStateBytes {
		t.Fatalf("state di %d byte", len(b))
	}
	if strings.Contains(string(b), "progetto-segreto") || strings.Contains(string(b), `"cwd"`) {
		t.Fatal("cwd nel messaggio per i display")
	}
	var m map[string]any
	_ = json.Unmarshal(b, &m)
	if m["defaultAvatar"] != strings.Repeat("b", 24) || len(m["sessions"].([]any)) != MaxSessions {
		t.Fatalf("campi: %v", m["defaultAvatar"])
	}
	for _, x := range m["sessions"].([]any) {
		if x.(map[string]any)["avatarCrc"] != float64(0xFFFFFFFF) {
			t.Fatal("avatarCrc")
		}
	}
	// la webapp invece vede cartella, progetto e origine
	v := s.View()
	if v.Sessions[0].Cwd == "" || v.Sessions[0].Project == "" || v.Sessions[0].AvatarFrom != "progetto" || v.DefaultAvatar == "" {
		t.Fatalf("view: %+v", v.Sessions[0])
	}
	t.Logf("state pieno con avatar: %d byte", len(b))
}
