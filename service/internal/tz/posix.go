// Package tz ricava la stringa POSIX TZ (quella che usa setenv("TZ") su ESP32) dal fuso locale.
//
// Metodo principale, valido su tutti i sistemi (anche Windows, dove non esistono file
// zoneinfo): si leggono dal *time.Location le transizioni dell'anno, si scrive la regola
// Mm.w.d/ora e la si verifica sugli anni successivi. Se la verifica fallisce (fusi con regole
// non esprimibili così), si prova il piè di pagina del file TZif locale (/etc/localtime),
// che contiene la stringa ufficiale di tzdata. In ogni caso la config `tz` può sostituirla.
package tz

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"
)

// Local restituisce la stringa POSIX del fuso locale e da dove viene ("calcolata", "tzif").
func Local(now time.Time) (string, string, error) {
	s, err := FromLocation(time.Local, now)
	if err == nil {
		return s, "calcolata", nil
	}
	for _, p := range localFiles() {
		if f, ferr := footerFile(p); ferr == nil {
			return f, "tzif", nil
		}
	}
	return s, "approssimata", err
}

// localFiles: i file TZif che descrivono il fuso locale, come li cerca la libreria di Go.
func localFiles() []string {
	name, ok := os.LookupEnv("TZ")
	if !ok {
		return []string{"/etc/localtime"}
	}
	name = strings.TrimPrefix(name, ":")
	if name == "" || strings.HasPrefix(name, "/") {
		return []string{name}
	}
	var out []string
	if d := os.Getenv("ZONEINFO"); d != "" {
		out = append(out, d+"/"+name)
	}
	for _, d := range []string{"/usr/share/zoneinfo/", "/usr/share/lib/zoneinfo/", "/usr/lib/locale/TZ/", "/etc/zoneinfo/"} {
		out = append(out, d+name)
	}
	return out
}

// FromLocation calcola la stringa POSIX valida per loc a partire dall'anno di now.
// Restituisce un errore (con la migliore approssimazione) se la regola ricavata non
// riproduce le transizioni dei 6 anni successivi.
func FromLocation(loc *time.Location, now time.Time) (string, error) {
	year := now.In(loc).Year()
	r, ok := rulesForYear(loc, year)
	if !ok {
		// Nessuna ora legale: basta il fuso fisso di metà anno.
		t := time.Date(year, 7, 1, 12, 0, 0, 0, loc)
		name, off := t.Zone()
		return posixName(name, off) + posixOffset(off), nil
	}
	s := r.String()
	for y := year + 1; y <= year+6; y++ {
		r2, ok2 := rulesForYear(loc, y)
		if !ok2 || r2.String() != s {
			return s, fmt.Errorf("regola del fuso %q non costante negli anni (%d: %q)", loc, y, r2.String())
		}
	}
	return s, nil
}

type zone struct {
	name string
	off  int // secondi a est di UTC
}

type transition struct {
	month, week, wday int
	secs              int // ora locale (prima della transizione) in secondi dalla mezzanotte
}

type rules struct {
	std, dst zone
	start    transition // ingresso nell'ora legale
	end      transition // ritorno all'ora solare
}

// rulesForYear trova le due transizioni dell'anno, se ci sono.
func rulesForYear(loc *time.Location, year int) (rules, bool) {
	var r rules
	var haveStart, haveEnd bool
	t := time.Date(year, 1, 1, 0, 0, 0, 0, loc)
	endYear := time.Date(year+1, 1, 1, 0, 0, 0, 0, loc)
	for i := 0; i < 8 && t.Before(endYear); i++ {
		_, end := t.ZoneBounds()
		if end.IsZero() || !end.Before(endYear) {
			break
		}
		before := end.Add(-time.Second)
		bName, bOff := before.Zone()
		aName, aOff := end.Zone()
		tr := makeTransition(end, bOff)
		if end.IsDST() && !before.IsDST() {
			r.std, r.dst, r.start = zone{bName, bOff}, zone{aName, aOff}, tr
			haveStart = true
		} else if !end.IsDST() && before.IsDST() {
			r.dst, r.std, r.end = zone{bName, bOff}, zone{aName, aOff}, tr
			haveEnd = true
		}
		t = end
	}
	return r, haveStart && haveEnd
}

// makeTransition esprime l'istante come "n-esima domenica (o altro giorno) del mese",
// in ora locale prima del cambio, come vuole POSIX.
func makeTransition(at time.Time, offBefore int) transition {
	l := at.UTC().Add(time.Duration(offBefore) * time.Second)
	day := l.Day()
	week := (day-1)/7 + 1
	if day+7 > daysIn(l.Month(), l.Year()) {
		week = 5 // ultima del mese
	}
	return transition{
		month: int(l.Month()),
		week:  week,
		wday:  int(l.Weekday()),
		secs:  l.Hour()*3600 + l.Minute()*60 + l.Second(),
	}
}

func daysIn(m time.Month, y int) int {
	return time.Date(y, m+1, 0, 0, 0, 0, 0, time.UTC).Day()
}

func (r rules) String() string {
	var b strings.Builder
	b.WriteString(posixName(r.std.name, r.std.off))
	b.WriteString(posixOffset(r.std.off))
	b.WriteString(posixName(r.dst.name, r.dst.off))
	if r.dst.off != r.std.off+3600 {
		b.WriteString(posixOffset(r.dst.off))
	}
	b.WriteString("," + r.start.String())
	b.WriteString("," + r.end.String())
	return b.String()
}

func (t transition) String() string {
	s := fmt.Sprintf("M%d.%d.%d", t.month, t.week, t.wday)
	if t.secs != 7200 { // le 2:00 sono il default POSIX
		s += "/" + hms(t.secs)
	}
	return s
}

// posixOffset: POSIX conta le ore a ovest di UTC, quindi il segno è invertito.
func posixOffset(eastSecs int) string {
	w := -eastSecs
	sign := ""
	if w < 0 {
		sign = "-"
		w = -w
	}
	return sign + hms(w)
}

func hms(secs int) string {
	h, m, s := secs/3600, secs/60%60, secs%60
	switch {
	case s != 0:
		return fmt.Sprintf("%d:%02d:%02d", h, m, s)
	case m != 0:
		return fmt.Sprintf("%d:%02d", h, m)
	}
	return fmt.Sprintf("%d", h)
}

// posixName: le abbreviazioni non alfabetiche (es. "+03") vanno tra < >.
func posixName(name string, off int) string {
	alpha := len(name) >= 3
	for _, c := range name {
		if !(c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') {
			alpha = false
		}
	}
	if alpha {
		return name
	}
	if name == "" {
		name = fmt.Sprintf("%+03d", off/3600) // nome sintetico tipo +03
	}
	return "<" + name + ">"
}

// footerFile legge la stringa POSIX in fondo a un file TZif v2+.
func footerFile(path string) (string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return Footer(b)
}

// Footer estrae la riga finale "\n<TZ>\n" di un file TZif v2 o successivo.
func Footer(b []byte) (string, error) {
	if len(b) < 5 || string(b[:4]) != "TZif" || b[4] < '2' {
		return "", errors.New("non è un file TZif v2+")
	}
	b = bytes.TrimSuffix(b, []byte("\n"))
	i := bytes.LastIndexByte(b, '\n')
	if i < 0 || i == len(b)-1 {
		return "", errors.New("piè di pagina TZif assente")
	}
	s := string(b[i+1:])
	if strings.ContainsAny(s, "\x00") {
		return "", errors.New("piè di pagina TZif non valido")
	}
	return s, nil
}
