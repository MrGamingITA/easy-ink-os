package game

import (
	"bytes"
	_ "embed"
	"encoding/json"
	"fmt"
	"slices"
)

//go:embed achievements.json
var achievementsJSON []byte

// Condition: traguardo raggiunto quando Counters[Counter] ≥ Min.
type Condition struct {
	Counter string `json:"counter"`
	Min     int    `json:"min"`
}

// Achievement: un traguardo di achievements.json.
type Achievement struct {
	ID          string    `json:"id"`
	Title       string    `json:"title"` // sullo schermo: ≤ 24 byte
	Description string    `json:"description"`
	When        Condition `json:"when"`
}

var achievements = mustParseAchievements(achievementsJSON)

// Achievements: i traguardi nell'ordine del file (copia).
func Achievements() []Achievement { return slices.Clone(achievements) }

func mustParseAchievements(data []byte) []Achievement {
	list, err := parseAchievements(data)
	if err != nil {
		panic("game: achievements.json: " + err.Error())
	}
	return list
}

func parseAchievements(data []byte) ([]Achievement, error) {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	var list []Achievement
	if err := dec.Decode(&list); err != nil {
		return nil, err
	}
	seen := map[string]bool{}
	for _, a := range list {
		switch {
		case a.ID == "" || seen[a.ID]:
			return nil, fmt.Errorf("id vuoto o doppio %q", a.ID)
		case a.Title == "" || len(a.Title) > 24:
			return nil, fmt.Errorf("%s: titolo vuoto o oltre 24 byte", a.ID)
		case a.Description == "" || len(a.Description) > 120:
			return nil, fmt.Errorf("%s: descrizione vuota o oltre 120 byte", a.ID)
		case !slices.Contains(knownCounters, a.When.Counter):
			return nil, fmt.Errorf("%s: contatore sconosciuto %q", a.ID, a.When.Counter)
		case a.When.Min < 1:
			return nil, fmt.Errorf("%s: soglia %d", a.ID, a.When.Min)
		}
		seen[a.ID] = true
	}
	return list, nil
}
