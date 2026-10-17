package server

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
)

func startThemes(t *testing.T) (*started, *web, string) {
	t.Helper()
	dir := filepath.Join(t.TempDir(), "themes")
	s := startOpts(t, false, 0, func(o *Options) { o.ThemeDir = dir })
	return s, newWeb(t, s), dir
}

func provaTheme(t *testing.T) []byte {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "themes", "fixtures", "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestWebThemesCRUD(t *testing.T) {
	_, w, dir := startThemes(t)
	if code, m := w.postRaw("/api/themes", provaTheme(t)); code != 201 || m["theme"].(map[string]any)["id"] != "prova" {
		t.Fatalf("crea: %d %v", code, m)
	}
	if _, err := os.Stat(filepath.Join(dir, "prova.json")); err != nil {
		t.Fatal(err)
	}
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 409 {
		t.Fatalf("doppione: %d", code)
	}
	code, m, _ := w.do(http.MethodGet, "/api/themes", "", "")
	if code != 200 || len(m["themes"].([]any)) != 1 {
		t.Fatalf("elenco: %d %v", code, m)
	}
	code, _, h := w.do(http.MethodGet, "/api/themes/prova", "", "")
	if code != 200 || h.Get("X-Calamaio-Crc") == "" || h.Get("ETag") == "" {
		t.Fatalf("file: %d %v", code, h)
	}
	body := strings.Replace(string(provaTheme(t)), "Mascotte grande", "Mascotte enorme", 1)
	code, m, _ = w.do(http.MethodPut, "/api/themes/prova", "application/json", body, w.same()...)
	if code != 200 || m["theme"].(map[string]any)["name"] != "Mascotte enorme" {
		t.Fatalf("modifica: %d %v", code, m)
	}
	code, m, _ = w.do(http.MethodGet, "/api/themes/prova/history", "", "")
	if code != 200 || len(m["versions"].([]any)) != 2 {
		t.Fatalf("cronologia: %d %v", code, m)
	}
	code, m, _ = w.do(http.MethodPost, "/api/themes/prova/restore/1", "", "", w.same()...)
	if code != 200 || m["theme"].(map[string]any)["name"] != "Mascotte grande" {
		t.Fatalf("ripristino: %d %v", code, m)
	}
	code, _, h = w.do(http.MethodPost, "/api/themes/prova/export", "", "", w.same()...)
	if code != 200 || !strings.Contains(h.Get("Content-Disposition"), `prova.theme.json`) {
		t.Fatalf("export: %d %v", code, h)
	}
	if code, m := w.postRaw("/api/themes/import", provaTheme(t)); code != 201 || m["theme"].(map[string]any)["id"] != "prova-2" {
		t.Fatalf("import: %d %v", code, m)
	}
	code, _, _ = w.do(http.MethodDelete, "/api/themes/prova-2", "", "", w.same()...)
	if code != 204 {
		t.Fatalf("cancella: %d", code)
	}
	code, _, _ = w.do(http.MethodDelete, "/api/themes/calamaio", "", "", w.same()...)
	if code != 409 {
		t.Fatalf("tema di serie: %d", code)
	}
}

func TestWebThemesInvalidAndGuards(t *testing.T) {
	_, w, _ := startThemes(t)
	bad := strings.Replace(string(provaTheme(t)), `"scale": 4 }`, `"scale": 9 }`, 1)
	code, m := w.postRaw("/api/themes", []byte(bad))
	if code != 400 || len(m["errors"].([]any)) == 0 || !strings.Contains(m["error"].(string), "scale") {
		t.Fatalf("non valido: %d %v", code, m)
	}
	// l'id del tema di serie non passa Parse: 400 (la PUT e la DELETE di /api/themes/calamaio danno 409)
	reserved := strings.Replace(string(provaTheme(t)), `"id": "prova"`, `"id": "calamaio"`, 1)
	if code, _ := w.postRaw("/api/themes", []byte(reserved)); code != 400 {
		t.Fatalf("id riservato: %d", code)
	}
	// altro sito: 403; senza JSON: 415
	code, _, _ = w.do(http.MethodPost, "/api/themes", "application/json", string(provaTheme(t)), "Origin", "http://evil.example", "Sec-Fetch-Site", "cross-site")
	if code != 403 {
		t.Fatalf("altro sito: %d", code)
	}
	code, _, _ = w.do(http.MethodPut, "/api/themes/prova", "text/plain", "x", w.same()...)
	if code != 415 {
		t.Fatalf("Content-Type: %d", code)
	}
}

func TestWebThemesDisabled(t *testing.T) {
	s := startOpts(t, false, 0, nil)
	w := newWeb(t, s)
	code, m, _ := w.do(http.MethodGet, "/api/themes", "", "")
	if code != 404 || m["error"] != "temi non disponibili" {
		t.Fatalf("%d %v", code, m)
	}
}

// themeBoard: scheda finta abbinata e autenticata che conosce i temi (ha già mandato themes).
func themeBoard(t *testing.T, s *started) *fakedisplay.Display {
	t.Helper()
	if err := s.keys.Put("calamaio-3f2a", testKey, time.Now()); err != nil {
		t.Fatal(err)
	}
	if err := s.run.Hub.LoadKeys(); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, fmt.Sprintf("ws://127.0.0.1:%d/ws", s.cfg.WSPort), "calamaio-3f2a", testKey, false,
		fakedisplay.Options{Themes: fakedisplay.ThemeOptions{Enabled: true}})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	for _, ev := range []string{"authed", "themes-sent"} {
		if _, err := d.Await(ev, 3*time.Second); err != nil {
			t.Fatal(err)
		}
	}
	return d
}

func TestWebThemeToDisplay(t *testing.T) {
	s, w, _ := startThemes(t)
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 201 {
		t.Fatal(code)
	}
	d := themeBoard(t, s)
	path := "/api/displays/" + d.Board + "/theme"
	// stesse guardie delle altre POST: prima di tutto, niente tema mandato
	if code, _, _ := w.do(http.MethodPost, path, "text/plain", `{"id":"prova"}`, "Origin", "http://"+w.host); code != 415 {
		t.Errorf("text/plain: %d", code)
	}
	if code, _, _ := w.do(http.MethodPost, path, "application/json", `{"id":"prova"}`, "Origin", "http://evil.example"); code != 403 {
		t.Errorf("altra origine: %d", code)
	}
	if _, err := d.Await("theme-offer", 300*time.Millisecond); err == nil {
		t.Fatal("una richiesta rifiutata ha mandato il tema")
	}
	code, m := w.post(path, `{"id":"prova"}`)
	if code != 202 || m["theme"].(map[string]any)["theme"] != "prova" {
		t.Fatalf("%d %v", code, m)
	}
	st := waitStatus(t, w, func(st map[string]any) bool {
		th, _ := firstDisplay(st)["themes"].(map[string]any)
		job, _ := th["job"].(map[string]any)
		return job != nil && job["stage"] == "fatto"
	})
	if firstDisplay(st)["themes"].(map[string]any)["cur"] != "prova" {
		t.Fatalf("%v", firstDisplay(st)["themes"])
	}
	if _, cur := d.Themes(); cur != "prova" {
		t.Fatalf("scheda: %q", cur)
	}
	if code, _ := w.post(path, `{"id":"manca"}`); code != 404 {
		t.Fatalf("tema sconosciuto: %d", code)
	}
	for _, body := range []string{`{"tema":"prova"}`, `{}`, `{"id":5}`, `[]`} {
		if code, _ := w.post(path, body); code != 400 {
			t.Fatalf("corpo %s: %d", body, code)
		}
	}
	if code, _ := w.post("/api/displays/calamaio-0000/theme", `{"id":"prova"}`); code != 409 {
		t.Fatalf("scheda sconosciuta: %d", code)
	}
	// di nuovo il tema di serie
	if code, m := w.post(path, `{"id":null}`); code != 202 || m["theme"].(map[string]any)["theme"] != nil {
		t.Fatalf("di serie: %d %v", code, m)
	}
	waitStatus(t, w, func(st map[string]any) bool {
		th, _ := firstDisplay(st)["themes"].(map[string]any)
		job, _ := th["job"].(map[string]any)
		return job != nil && job["stage"] == "fatto" && job["theme"] == nil && th["cur"] == nil
	})
}

func themeWithPerm(t *testing.T) []byte {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(provaTheme(t), &m); err != nil {
		t.Fatal(err)
	}
	sc := m["screens"].(map[string]any)
	sc["perm"] = sc["home"]
	b, _ := json.Marshal(m)
	return b
}

func TestAPIThemeCheck(t *testing.T) {
	_, w, dir := startThemes(t)
	code, m := w.postRaw("/api/themes/check", provaTheme(t))
	th, _ := m["theme"].(map[string]any)
	if code != 200 || th["screens"].(map[string]any)["home"].(float64) == 0 {
		t.Fatalf("valido: %d %v", code, m)
	}
	code, m = w.postRaw("/api/themes/check", []byte(`{rotto`))
	th, _ = m["theme"].(map[string]any)
	if code != 200 || len(th["problems"].([]any)) == 0 {
		t.Fatalf("JSON rotto: %d %v", code, m)
	}
	// Permessi non si controlla come valido
	code, m = w.postRaw("/api/themes/check", themeWithPerm(t))
	th, _ = m["theme"].(map[string]any)
	if code != 200 || !strings.Contains(fmt.Sprint(th["problems"]), "screens.perm") {
		t.Fatalf("perm: %d %v", code, m)
	}
	if code, _ := w.postRaw("/api/themes/check", []byte(strings.Repeat(" ", 256<<10+1))); code != 413 {
		t.Fatalf("troppo grande: %d", code)
	}
	code, _, _ = w.do(http.MethodPost, "/api/themes/check", "application/json", string(provaTheme(t)), "Origin", "http://evil.example", "Sec-Fetch-Site", "cross-site")
	if code != 403 {
		t.Fatalf("altro sito: %d", code)
	}
	code, _, _ = w.do(http.MethodPost, "/api/themes/check", "text/plain", "x", w.same()...)
	if code != 415 {
		t.Fatalf("Content-Type: %d", code)
	}
	if ents, _ := os.ReadDir(dir); len(ents) != 0 {
		t.Fatalf("il controllo ha scritto: %v", ents)
	}
}

func TestAPIThemeAutosave(t *testing.T) {
	_, w, _ := startThemes(t)
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 201 {
		t.Fatal(code)
	}
	code, _, h := w.do(http.MethodGet, "/api/themes/prova", "", "")
	base := h.Get("X-Calamaio-Updated")
	if code != 200 || base == "" {
		t.Fatalf("GET senza X-Calamaio-Updated: %d %v", code, h)
	}
	body := strings.Replace(string(provaTheme(t)), "Mascotte grande", "Mascotte auto", 1)
	code, m, _ := w.do(http.MethodPut, "/api/themes/prova?autosave=1&base="+base, "application/json", body, w.same()...)
	th, _ := m["theme"].(map[string]any)
	if code != 200 || th["name"] != "Mascotte auto" {
		t.Fatalf("autosave: %d %v", code, m)
	}
	// la base di prima ormai è vecchia (a meno che il salvataggio cada nello stesso millisecondo)
	updated := fmt.Sprintf("%.0f", th["updated"].(float64))
	if updated == base {
		time.Sleep(5 * time.Millisecond)
		body2 := strings.Replace(body, "Mascotte auto", "Mascotte due", 1)
		_, m, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1&base="+updated, "application/json", body2, w.same()...)
		th, _ = m["theme"].(map[string]any)
		updated = fmt.Sprintf("%.0f", th["updated"].(float64))
	}
	code, m, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1&base="+base, "application/json", body, w.same()...)
	live, _ := m["theme"].(map[string]any)
	if code != 409 || m["error"] != "il tema è stato cambiato altrove" || live == nil || fmt.Sprintf("%.0f", live["updated"].(float64)) != updated {
		t.Fatalf("conflitto: %d %v (live %s)", code, m, updated)
	}
	if code, _, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1&base=x", "application/json", body, w.same()...); code != 400 {
		t.Fatalf("base non numerica: %d", code)
	}
	// senza autosave=1 resta la PUT di sempre, senza controllo della base
	if code, _, _ = w.do(http.MethodPut, "/api/themes/prova?base="+base, "application/json", body, w.same()...); code != 200 {
		t.Fatalf("PUT normale: %d", code)
	}
	code, _, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1", "application/json", body, "Origin", "http://evil.example", "Sec-Fetch-Site", "cross-site")
	if code != 403 {
		t.Fatalf("altro sito: %d", code)
	}
}

// La schermata Permessi non si salva da nessuna strada.
func TestAPIThemePermRejected(t *testing.T) {
	_, w, dir := startThemes(t)
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 201 {
		t.Fatal(code)
	}
	before, _ := os.ReadFile(filepath.Join(dir, "prova.json"))
	for _, q := range []string{"?autosave=1", "?autosave=1&base=1", ""} {
		code, m, _ := w.do(http.MethodPut, "/api/themes/prova"+q, "application/json", string(themeWithPerm(t)), w.same()...)
		if code != 400 || !strings.Contains(fmt.Sprint(m["errors"]), "screens.perm") {
			t.Fatalf("PUT%s: %d %v", q, code, m)
		}
	}
	after, _ := os.ReadFile(filepath.Join(dir, "prova.json"))
	if string(before) != string(after) {
		t.Fatal("il file è cambiato")
	}
}

// «Sovrascrivi» (fresh=1): versione nuova anche dentro la finestra d'unione; la base vecchia resta un 409.
func TestAPIThemeAutosaveFresh(t *testing.T) {
	_, w, _ := startThemes(t)
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 201 {
		t.Fatal(code)
	}
	_, _, h := w.do(http.MethodGet, "/api/themes/prova", "", "")
	stale := h.Get("X-Calamaio-Updated")
	versions := func() int {
		_, m, _ := w.do(http.MethodGet, "/api/themes/prova/history", "", "")
		vs, _ := m["versions"].([]any)
		return len(vs)
	}
	time.Sleep(5 * time.Millisecond)
	mac := strings.Replace(string(provaTheme(t)), "Mascotte grande", "Mascotte Mac", 1)
	code, m, _ := w.do(http.MethodPut, "/api/themes/prova?autosave=1&base="+stale, "application/json", mac, w.same()...)
	if code != 200 {
		t.Fatalf("autosave Mac: %d %v", code, m)
	}
	live := fmt.Sprintf("%.0f", m["theme"].(map[string]any)["updated"].(float64))
	if versions() != 2 {
		t.Fatalf("versioni prima: %d", versions())
	}
	phone := strings.Replace(string(provaTheme(t)), "Mascotte grande", "Mascotte iPhone", 1)
	if code, _, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1&fresh=1&base="+stale, "application/json", phone, w.same()...); code != 409 {
		t.Fatalf("fresh con base vecchia: %d", code)
	}
	time.Sleep(5 * time.Millisecond)
	if code, m, _ = w.do(http.MethodPut, "/api/themes/prova?autosave=1&fresh=1&base="+live, "application/json", phone, w.same()...); code != 200 {
		t.Fatalf("sovrascrivi: %d %v", code, m)
	}
	if versions() != 3 {
		t.Fatalf("versioni dopo: %d, attese 3", versions())
	}
}
