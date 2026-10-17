// Package webui serve la webapp di Calamaio (file in static/, inclusi nel binario con go:embed).
package webui

import (
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"io/fs"
	"net/http"
	"path"
	"strconv"
	"strings"
)

//go:embed static
var embedded embed.FS

// Static è la cartella static/ inclusa nel binario.
func Static() fs.FS {
	sub, err := fs.Sub(embedded, "static")
	if err != nil {
		panic(err)
	}
	return sub
}

// Content-Type espliciti: mime.TypeByExtension dipende dal sistema (su Windows il registro può
// dire text/plain per .js, e con nosniff il browser rifiuterebbe il modulo).
var types = map[string]string{
	".html":        "text/html; charset=utf-8",
	".js":          "text/javascript; charset=utf-8",
	".mjs":         "text/javascript; charset=utf-8",
	".css":         "text/css; charset=utf-8",
	".json":        "application/json",
	".map":         "application/json",
	".svg":         "image/svg+xml",
	".png":         "image/png",
	".jpg":         "image/jpeg",
	".jpeg":        "image/jpeg",
	".gif":         "image/gif",
	".webp":        "image/webp",
	".ico":         "image/x-icon",
	".woff":        "font/woff",
	".woff2":       "font/woff2",
	".ttf":         "font/ttf",
	".otf":         "font/otf",
	".wasm":        "application/wasm",
	".txt":         "text/plain; charset=utf-8",
	".md":          "text/plain; charset=utf-8",
	".pbm":         "image/x-portable-bitmap",
	".bin":         "application/octet-stream",
	".webmanifest": "application/manifest+json",
}

// ContentType restituisce il tipo per estensione (application/octet-stream se sconosciuta).
func ContentType(name string) string {
	if t, ok := types[strings.ToLower(path.Ext(name))]; ok {
		return t
	}
	return "application/octet-stream"
}

type file struct {
	data  []byte
	etag  string
	ctype string
}

// Handler serve i file. / → index.html; un percorso senza estensione che non esiste → index.html
// (navigazione lato client); il resto → 404. Cache: no-cache con ETag (il browser rivalida a ogni
// caricamento, risposte 304 senza corpo): dopo un aggiornamento del servizio la UI non resta vecchia.
type Handler struct {
	files map[string]file
}

func New(fsys fs.FS) *Handler {
	h := &Handler{files: map[string]file{}}
	_ = fs.WalkDir(fsys, ".", func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		b, err := fs.ReadFile(fsys, p)
		if err != nil {
			return err
		}
		sum := sha256.Sum256(b)
		h.files["/"+p] = file{data: b, etag: `"` + hex.EncodeToString(sum[:8]) + `"`, ctype: ContentType(p)}
		return nil
	})
	return h
}

// Has dice se un file è incluso (test).
func (h *Handler) Has(p string) bool { _, ok := h.files[p]; return ok }

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		w.Header().Set("Allow", "GET, HEAD")
		http.Error(w, "metodo non ammesso", http.StatusMethodNotAllowed)
		return
	}
	p := path.Clean("/" + r.URL.Path)
	if strings.HasSuffix(p, "/") {
		p += "index.html"
	}
	f, ok := h.files[p]
	if !ok && path.Ext(p) == "" {
		f, ok = h.files["/index.html"]
	}
	if !ok {
		http.NotFound(w, r)
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", f.ctype)
	hd.Set("Cache-Control", "no-cache")
	hd.Set("ETag", f.etag)
	if match := r.Header.Get("If-None-Match"); match != "" && etagMatch(match, f.etag) {
		w.WriteHeader(http.StatusNotModified)
		return
	}
	hd.Set("Content-Length", strconv.Itoa(len(f.data)))
	w.WriteHeader(http.StatusOK)
	if r.Method == http.MethodGet {
		_, _ = w.Write(f.data)
	}
}

func etagMatch(header, etag string) bool {
	for _, t := range strings.Split(header, ",") {
		t = strings.TrimSpace(t)
		if t == "*" || strings.TrimPrefix(t, "W/") == etag {
			return true
		}
	}
	return false
}
