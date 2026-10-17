package api

import (
	"errors"
	"mime"
	"net/http"
	"net/url"
	"strconv"

	"calamaio/internal/backup"
)

// API dei backup della flash (forme di tools/webui-dev/mock-server.mjs e static/assets/js/api.js):
//
//	GET    /api/backups            → 200 {backups: [Backup]}
//	POST   /api/backups?name=…     → 201 {backup} · 400 · 413 · 415 (corpo application/octet-stream)
//	GET    /api/backups/{id}/data  → i byte (application/octet-stream)
//	DELETE /api/backups/{id}       → 204 · 403 protetto · 404

// Parametri ammessi nella query della POST; ogni altro è un 400 (errori di battitura visibili subito).
var backupParams = map[string]bool{"name": true, "chip": true, "mac": true, "board": true,
	"flashSize": true, "offset": true, "note": true, "sha256": true}

func (s *Server) noBackups(w http.ResponseWriter) bool {
	if s.Backups == nil {
		writeJSON(w, http.StatusNotFound, errBody("archivio dei backup non disponibile"))
		return true
	}
	return false
}

func (s *Server) apiBackups(w http.ResponseWriter, _ *http.Request) {
	if s.noBackups(w) {
		return
	}
	list, err := s.Backups.List()
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody("elenco dei backup illeggibile: "+err.Error()))
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"backups": list})
}

func (s *Server) apiBackupCreate(w http.ResponseWriter, r *http.Request) {
	if s.noBackups(w) {
		return
	}
	if r.ContentLength > backup.MaxSize {
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody(backup.ErrTooLarge.Error()))
		return
	}
	q, err := url.ParseQuery(r.URL.RawQuery)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("parametri non leggibili"))
		return
	}
	for k, v := range q {
		if !backupParams[k] {
			writeJSON(w, http.StatusBadRequest, errBody("parametro sconosciuto: "+k))
			return
		}
		if len(v) != 1 {
			writeJSON(w, http.StatusBadRequest, errBody("parametro ripetuto: "+k))
			return
		}
	}
	m := backup.Meta{Name: q.Get("name"), Chip: q.Get("chip"), MAC: q.Get("mac"), Board: q.Get("board"),
		Note: q.Get("note"), SHA256: q.Get("sha256")}
	if m.FlashSize, err = backup.ParseSize("flashSize", q.Get("flashSize")); err == nil {
		m.Offset, err = backup.ParseSize("offset", q.Get("offset"))
	}
	if err != nil {
		writeJSON(w, http.StatusBadRequest, errBody(err.Error()))
		return
	}
	// MaxBytesReader: oltre il limite il server smette di leggere e chiude la connessione.
	body := http.MaxBytesReader(w, r.Body, backup.MaxSize+1)
	b, err := s.Backups.Create(m, body)
	var inv *backup.InvalidError
	var ce *backup.ChecksumError
	var mbe *http.MaxBytesError
	switch {
	case err == nil:
		w.Header().Set("Location", "/api/backups/"+url.PathEscape(b.ID))
		writeJSON(w, http.StatusCreated, map[string]any{"backup": b})
		if s.Log != nil {
			s.Log.Printf("backup salvato: %s «%s» (%d byte, sha256 %s)", b.ID, b.Name, b.Size, b.SHA256)
		}
	case errors.Is(err, backup.ErrTooLarge), errors.As(err, &mbe):
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody(backup.ErrTooLarge.Error()))
	case errors.As(err, &inv):
		writeJSON(w, http.StatusBadRequest, errBody(inv.Msg))
	case errors.As(err, &ce), errors.Is(err, backup.ErrEmpty):
		writeJSON(w, http.StatusBadRequest, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody("backup non salvato: "+err.Error()))
	}
}

func (s *Server) apiBackupData(w http.ResponseWriter, r *http.Request) {
	if s.noBackups(w) {
		return
	}
	f, b, err := s.Backups.OpenData(r.PathValue("id"))
	if errors.Is(err, backup.ErrNotFound) {
		writeJSON(w, http.StatusNotFound, errBody("Backup sconosciuto."))
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": backup.FileName(b)}))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Sha256", b.SHA256)
	hd.Set("X-Calamaio-Offset", strconv.FormatInt(b.Offset, 10))
	http.ServeContent(w, r, "", st.ModTime(), f)
}

func (s *Server) apiBackupDelete(w http.ResponseWriter, r *http.Request) {
	if s.noBackups(w) {
		return
	}
	id := r.PathValue("id")
	err := s.Backups.Delete(id)
	switch {
	case err == nil:
		if s.Log != nil {
			s.Log.Printf("backup eliminato: %s", id)
		}
		w.Header().Set("Cache-Control", "no-store")
		w.WriteHeader(http.StatusNoContent)
	case errors.Is(err, backup.ErrProtected):
		if s.Log != nil {
			s.Log.Printf("rifiutata l'eliminazione del backup protetto %s", id)
		}
		writeJSON(w, http.StatusForbidden, errBody("Backup protetto: l'originale di fabbrica non si elimina."))
	case errors.Is(err, backup.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody("Backup sconosciuto."))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	}
}
