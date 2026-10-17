package api

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"

	"calamaio/internal/display"
	"calamaio/internal/theme"
)

const maxThemeBody = 256 << 10

func (s *Server) themesOff(w http.ResponseWriter) bool {
	if s.Themes == nil {
		writeJSON(w, http.StatusNotFound, errBody("temi non disponibili"))
		return true
	}
	return false
}

// themeError: errori dell'archivio → risposte della API (docs/PROTOCOLLO.md «Temi»).
func themeError(w http.ResponseWriter, err error) {
	var ie *theme.InvalidError
	switch {
	case errors.As(err, &ie):
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": ie.Error(), "errors": ie.Problems})
	case errors.Is(err, theme.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody(err.Error()))
	case errors.Is(err, theme.ErrExists), errors.Is(err, theme.ErrReserved):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	}
}

func readTheme(w http.ResponseWriter, r *http.Request) ([]byte, bool) {
	b, err := io.ReadAll(io.LimitReader(r.Body, maxThemeBody+1))
	if err != nil || len(b) > maxThemeBody {
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody("tema oltre 256 KB"))
		return nil, false
	}
	return b, true
}

func (s *Server) apiThemes(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	list, err := s.Themes.List()
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"themes": list, "dir": s.Themes.Dir})
}

func (s *Server) apiThemeCreate(isImport bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if s.themesOff(w) {
			return
		}
		b, ok := readTheme(w, r)
		if !ok {
			return
		}
		var in *theme.Info
		var err error
		if isImport {
			in, err = s.Themes.Import(b)
		} else {
			in, err = s.Themes.Create(b)
		}
		if err != nil {
			themeError(w, err)
			return
		}
		writeJSON(w, http.StatusCreated, map[string]any{"theme": in})
	}
}

func (s *Server) apiThemeFile(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	data, in, err := s.Themes.Get(r.PathValue("id"))
	if err != nil {
		themeError(w, err)
		return
	}
	crc := fmt.Sprintf("%08x", in.CRC)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("ETag", `"`+crc+`"`)
	w.Header().Set("X-Calamaio-Crc", crc)
	w.Header().Set("X-Calamaio-Updated", strconv.FormatInt(in.Updated, 10))
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = w.Write(data)
}

func (s *Server) apiThemePut(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	b, ok := readTheme(w, r)
	if !ok {
		return
	}
	id := r.PathValue("id")
	var in *theme.Info
	var err error
	if r.URL.Query().Get("autosave") == "1" {
		var base int64
		if v := r.URL.Query().Get("base"); v != "" {
			if base, err = strconv.ParseInt(v, 10, 64); err != nil || base < 0 {
				writeJSON(w, http.StatusBadRequest, errBody("base: millisecondi dell'ultima modifica"))
				return
			}
		}
		if r.URL.Query().Get("fresh") == "1" { // «Sovrascrivi»: versione nuova, niente unione
			in, err = s.Themes.AutosaveFresh(id, b, base)
		} else {
			in, err = s.Themes.Autosave(id, b, base)
		}
		if errors.Is(err, theme.ErrChanged) {
			body := map[string]any{"error": err.Error()}
			if _, live, gerr := s.Themes.Get(id); gerr == nil {
				body["theme"] = live
			}
			writeJSON(w, http.StatusConflict, body)
			return
		}
	} else {
		in, err = s.Themes.Put(id, b)
	}
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"theme": in})
}

// apiThemeCheck: POST /api/themes/check, valida senza salvare. 200 {theme: Info} anche se non valido.
func (s *Server) apiThemeCheck(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	b, ok := readTheme(w, r)
	if !ok {
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"theme": s.Themes.Check(b)})
}

func (s *Server) apiThemeDelete(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	if err := s.Themes.Delete(r.PathValue("id")); err != nil {
		themeError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) apiThemeHistory(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	vs, err := s.Themes.History(r.PathValue("id"))
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"versions": vs})
}

func (s *Server) apiThemeRestore(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	n, err := strconv.Atoi(r.PathValue("n"))
	if err != nil {
		writeJSON(w, http.StatusNotFound, errBody("versione sconosciuta"))
		return
	}
	in, err := s.Themes.Restore(r.PathValue("id"), n)
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"theme": in})
}

func (s *Server) apiThemeExport(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	id := r.PathValue("id")
	data, _, err := s.Themes.Get(id)
	if err != nil {
		themeError(w, err)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Disposition", `attachment; filename="`+id+`.theme.json"`)
	_, _ = w.Write(data)
}

// ThemeSender: mandare un tema a una scheda (display.Hub).
type ThemeSender interface {
	SendTheme(board string, p *display.ThemePayload) (*display.ThemeJobView, error)
}

// apiDisplayTheme: POST /api/displays/{board}/theme {"id": "tema" | null}. 202 con l'operazione (si segue in
// /api/status, displays[].themes.job); la scheda abbinata ma scollegata la riceve quando torna.
func (s *Server) apiDisplayTheme(w http.ResponseWriter, r *http.Request) {
	if s.ThemeSend == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	var body struct {
		ID json.RawMessage `json:"id"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 1<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil || body.ID == nil {
		writeJSON(w, http.StatusBadRequest, errBody(`serve un oggetto JSON {"id": "tema" | null}`))
		return
	}
	var p *display.ThemePayload
	if string(body.ID) != "null" {
		var id string
		if json.Unmarshal(body.ID, &id) != nil {
			writeJSON(w, http.StatusBadRequest, errBody("id: una stringa o null"))
			return
		}
		if s.themesOff(w) {
			return
		}
		// una sola lettura del file: nome e CTHM vengono dalla stessa versione (come Store.Compiled)
		data, _, err := s.Themes.Get(id)
		if err != nil {
			themeError(w, err)
			return
		}
		th, c, err := theme.CompileJSON(data)
		if err != nil {
			writeJSON(w, http.StatusConflict, errBody("tema non valido: "+err.Error()))
			return
		}
		p = &display.ThemePayload{ID: th.ID, Name: th.Name, Screen: th.Screen, CRC: c.CRC, Bin: c.Bin}
	}
	v, err := s.ThemeSend.SendTheme(r.PathValue("board"), p)
	var blk *display.BlockedError
	switch {
	case errors.As(err, &blk): // schermo diverso: commandError non conosce BlockedError (darebbe 500), come otaError
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
		return
	case err != nil:
		commandError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"theme": v})
}
