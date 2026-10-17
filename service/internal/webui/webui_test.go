package webui

import (
	"io/fs"
	"net/http"
	"net/http/httptest"
	"regexp"
	"strings"
	"testing"
	"testing/fstest"
)

func TestServeFiles(t *testing.T) {
	h := New(fstest.MapFS{
		"index.html":           {Data: []byte("<!doctype html><title>x</title>")},
		"assets/js/app.js":     {Data: []byte("export {}")},
		"assets/fonts/a.woff2": {Data: []byte{0, 1}},
		"assets/app.css":       {Data: []byte("body{}")},
	})
	get := func(p string, hdr ...string) *httptest.ResponseRecorder {
		r := httptest.NewRequest(http.MethodGet, p, nil)
		if len(hdr) == 2 {
			r.Header.Set(hdr[0], hdr[1])
		}
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}
	cases := map[string]string{
		"/":                     "text/html; charset=utf-8",
		"/index.html":           "text/html; charset=utf-8",
		"/assets/js/app.js":     "text/javascript; charset=utf-8",
		"/assets/fonts/a.woff2": "font/woff2",
		"/assets/app.css":       "text/css; charset=utf-8",
		"/dispositivo":          "text/html; charset=utf-8", // navigazione lato client
	}
	for p, ct := range cases {
		w := get(p)
		if w.Code != 200 || w.Header().Get("Content-Type") != ct || w.Header().Get("Cache-Control") != "no-cache" || w.Header().Get("ETag") == "" {
			t.Errorf("%s: %d %q %q", p, w.Code, w.Header().Get("Content-Type"), w.Header().Get("Cache-Control"))
		}
	}
	for _, p := range []string{"/manca.js", "/../../etc/passwd.txt", "/assets/js/x.css"} {
		if w := get(p); w.Code != 404 {
			t.Errorf("%s: %d", p, w.Code)
		}
	}
	etag := get("/assets/js/app.js").Header().Get("ETag")
	if w := get("/assets/js/app.js", "If-None-Match", etag); w.Code != http.StatusNotModified || w.Body.Len() != 0 {
		t.Fatalf("If-None-Match: %d", w.Code)
	}
	r := httptest.NewRequest(http.MethodPost, "/", nil)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("POST /: %d", w.Code)
	}
}

// I file veri inclusi nel binario: almeno uno, e nessun tipo sconosciuto.
func TestEmbeddedTypes(t *testing.T) {
	h := New(Static())
	if len(h.files) == 0 {
		t.Fatal("static/ vuota")
	}
	for p, f := range h.files {
		if f.ctype == "application/octet-stream" {
			t.Errorf("%s: Content-Type sconosciuto (aggiungi l'estensione in types)", p)
		}
	}
	if !h.Has("/index.html") {
		t.Log("static/index.html non c'è ancora: GET / risponde 404")
	}
}

// CSP senza 'unsafe-inline': nelle pagine niente script inline, niente attributi style o on….
func TestNoInlineInPages(t *testing.T) {
	re := regexp.MustCompile(`(?i)<script(?:\s[^>]*)?>\s*[^<\s]|\sstyle\s*=|\son[a-z]+\s*=|<style`)
	pages, _ := fs.Glob(Static(), "*.html")
	if len(pages) < 5 {
		t.Fatalf("pagine: %v", pages)
	}
	for _, p := range pages {
		b, _ := fs.ReadFile(Static(), p)
		if m := re.Find(b); m != nil {
			t.Errorf("%s: inline %q", p, m)
		}
		if !strings.Contains(string(b), `href="avatars.html"`) {
			t.Errorf("%s: manca il collegamento alla pagina Avatar", p)
		}
		if !strings.Contains(string(b), `href="game.html"`) {
			t.Errorf("%s: manca il collegamento alla pagina Gioco", p)
		}
	}
}
