package game

import "testing"

func TestLevelCost(t *testing.T) {
	// round(100 × n^1.3)
	cases := map[int]int{1: 100, 2: 246, 3: 417, 4: 606, 10: 1995}
	for n, want := range cases {
		if got := LevelCost(n); got != want {
			t.Errorf("LevelCost(%d) = %d, voglio %d", n, got, want)
		}
	}
}

func TestLevelStartAndLevelFor(t *testing.T) {
	if got := LevelStart(1); got != 0 {
		t.Errorf("LevelStart(1) = %d, voglio 0", got)
	}
	if got := LevelStart(2); got != 100 {
		t.Errorf("LevelStart(2) = %d, voglio 100", got)
	}
	// dal piano: livello 3 a 100 + round(100×2^1.3) = 346
	if got := LevelStart(3); got != 346 {
		t.Errorf("LevelStart(3) = %d, voglio 346", got)
	}
	cases := map[int]int{0: 1, 99: 1, 100: 2, 345: 2, 346: 3, 7693: 9, 7694: 10}
	for xp, want := range cases {
		if got := LevelFor(xp); got != want {
			t.Errorf("LevelFor(%d) = %d, voglio %d", xp, got, want)
		}
	}
}

func TestHalfRoundsUp(t *testing.T) {
	cases := map[int]int{10: 5, 15: 8, 20: 10, 25: 13}
	for in, want := range cases {
		if got := half(in); got != want {
			t.Errorf("half(%d) = %d, voglio %d", in, got, want)
		}
	}
}

func TestBaseXP(t *testing.T) {
	cases := map[string]int{
		KindTaskDone: 10, KindTurnDone: 20, KindTestGreen: 15, KindSessionClosed: 10,
		KindActivity: 0, KindTestOK: 0, KindTestFail: 0,
	}
	for k, want := range cases {
		got, ok := baseXP(k)
		if !ok || got != want {
			t.Errorf("baseXP(%q) = %d,%v, voglio %d,true", k, got, ok, want)
		}
	}
	// nessun Kind per permessi, costi, token, notte, ore filate
	for _, k := range []string{"permission", "permission_approved", "cost", "tokens", "night", "hours", ""} {
		if _, ok := baseXP(k); ok {
			t.Errorf("baseXP(%q) accettato: non deve esistere", k)
		}
	}
}
