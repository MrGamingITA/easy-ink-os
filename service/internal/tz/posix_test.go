package tz

import (
	"os"
	"testing"
	"time"
)

func TestFromLocation(t *testing.T) {
	now := time.Date(2026, 10, 11, 12, 0, 0, 0, time.UTC)
	cases := map[string]string{
		"Europe/Rome":         "CET-1CEST,M3.5.0,M10.5.0/3",
		"Europe/London":       "GMT0BST,M3.5.0/1,M10.5.0",
		"America/New_York":    "EST5EDT,M3.2.0,M11.1.0",
		"America/Los_Angeles": "PST8PDT,M3.2.0,M11.1.0",
		"Australia/Sydney":    "AEST-10AEDT,M10.1.0,M4.1.0/3",
		"Asia/Kolkata":        "IST-5:30",
		"Asia/Tokyo":          "JST-9",
		"America/Sao_Paulo":   "<-03>3",
		"UTC":                 "UTC0",
		"Australia/Lord_Howe": "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
	}
	for name, want := range cases {
		loc, err := time.LoadLocation(name)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		got, err := FromLocation(loc, now)
		if err != nil {
			t.Errorf("%s: %v", name, err)
		}
		if got != want {
			t.Errorf("%s = %q, atteso %q", name, got, want)
		}
	}
}

// Il calcolo deve coincidere con il piè di pagina ufficiale di tzdata, dove il file esiste.
func TestMatchesTZifFooter(t *testing.T) {
	now := time.Date(2026, 10, 11, 12, 0, 0, 0, time.UTC)
	names := []string{"Europe/Rome", "Europe/Berlin", "Europe/Lisbon", "America/Chicago",
		"America/Denver", "America/Phoenix", "Pacific/Auckland", "Asia/Tehran", "Africa/Cairo",
		"America/Havana", "America/Santiago", "Asia/Gaza", "Asia/Jerusalem", "Europe/Dublin"}
	for _, n := range names {
		b, err := os.ReadFile("/usr/share/zoneinfo/" + n)
		if err != nil {
			t.Skipf("zoneinfo di sistema assente: %v", err)
		}
		want, err := Footer(b)
		if err != nil {
			t.Fatalf("%s: %v", n, err)
		}
		loc, _ := time.LoadLocation(n)
		got, ferr := FromLocation(loc, now)
		if got != want {
			t.Logf("%s: calcolata %q, tzdata %q (errore di verifica: %v)", n, got, want, ferr)
			if ferr == nil {
				t.Errorf("%s: regola diversa da tzdata ma verifica superata", n)
			}
		}
	}
}

func TestLocal(t *testing.T) {
	s, src, err := Local(time.Now())
	if s == "" {
		t.Fatalf("stringa vuota (%s, %v)", src, err)
	}
	t.Logf("fuso locale: %s (%s)", s, src)
}
