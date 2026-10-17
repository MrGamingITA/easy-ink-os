package game

import (
	"slices"
	"testing"
)

func TestAchievementsFile(t *testing.T) {
	list := Achievements()
	if len(list) < 18 || len(list) > 24 {
		t.Errorf("traguardi: %d, voglio circa 20", len(list))
	}
	ids := map[string]bool{}
	for _, a := range list {
		if a.ID == "" || ids[a.ID] {
			t.Errorf("id vuoto o doppio: %q", a.ID)
		}
		ids[a.ID] = true
		// il titolo va sullo schermo: al massimo 24 byte (le lettere accentate valgono 2)
		if a.Title == "" || len(a.Title) > 24 {
			t.Errorf("%s: titolo %q di %d byte", a.ID, a.Title, len(a.Title))
		}
		if a.Description == "" || len(a.Description) > 120 {
			t.Errorf("%s: descrizione di %d byte", a.ID, len(a.Description))
		}
		if !slices.Contains(knownCounters, a.When.Counter) {
			t.Errorf("%s: contatore sconosciuto %q", a.ID, a.When.Counter)
		}
		if a.When.Min < 1 {
			t.Errorf("%s: soglia %d", a.ID, a.When.Min)
		}
	}
	// i traguardi chiesti dalla specifica
	for _, id := range []string{"prima-sessione", "task-10", "task-100", "task-1000", "serie-7", "serie-30", "verdi-10", "tre-insieme", "pause-settimana"} {
		if !ids[id] {
			t.Errorf("manca il traguardo %s", id)
		}
	}
}

func TestParseAchievementsRejectsBadData(t *testing.T) {
	bad := []string{
		`[{"id":"a","title":"A","description":"d","when":{"counter":"permissions","min":1}}]`,
		`[{"id":"a","title":"Un titolo davvero troppo lungo","description":"d","when":{"counter":"level","min":1}}]`,
		`[{"id":"a","title":"A","description":"d","when":{"counter":"level","min":0}}]`,
		`[{"id":"a","title":"A","description":"d","when":{"counter":"level","min":1}},{"id":"a","title":"B","description":"d","when":{"counter":"level","min":2}}]`,
		`[{"id":"a","title":"A","description":"d","when":{"counter":"level","min":1},"extra":1}]`,
	}
	for _, b := range bad {
		if _, err := parseAchievements([]byte(b)); err == nil {
			t.Errorf("accettato: %s", b)
		}
	}
}
