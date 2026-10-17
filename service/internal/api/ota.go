package api

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
	"time"

	"calamaio/internal/diag"
	"calamaio/internal/display"
	"calamaio/internal/firmware"
	"calamaio/internal/ota"
)

// Aggiornamento remoto, registro da remoto e crash per la webapp e per `calamaio ota push` e
// `calamaio logs` (docs/OTA.md, forme in docs/PROTOCOLLO.md «API locale»).

// OTAControl: aggiornamento e registro delle schede (display.Hub).
type OTAControl interface {
	StartOTA(board string, img display.OTAImage) (*display.OtaView, error)
	CancelOTA(board string) (*display.OtaView, error)
	OtaKeyPub() (string, error)
	OtaJobs() []display.OtaView
	LogStart(board string, follow bool, minutes int) (*display.LogView, error)
	LogStop(board string) (*display.LogView, error)
	LogRead(board string, after int64) ([]display.LogLine, int64, *display.LogView, <-chan struct{})
}

// OTAStatus è la parte `ota` di GET /api/status.
type OTAStatus struct {
	Key      bool   `json:"key"`                // la chiave di firma c'è
	Pub      string `json:"pub,omitempty"`      // chiave pubblica (64 cifre)
	KeyError string `json:"keyError,omitempty"` // perché non si può usare
	// Jobs: per scheda l'aggiornamento in corso o l'ultimo, anche se la scheda adesso è scollegata
	// (durante il riavvio).
	Jobs []display.OtaView `json:"jobs"`
}

// MaxOTALogWait: lunga attesa al massimo di GET …/log?wait=.
const MaxOTALogWait = 25 * time.Second

func (s *Server) otaStatus() *OTAStatus {
	if s.OTA == nil {
		return nil
	}
	pub, err := s.OTA.OtaKeyPub()
	st := &OTAStatus{Key: err == nil, Pub: pub, Jobs: s.OTA.OtaJobs()}
	if err != nil {
		st.KeyError = err.Error()
	}
	return st
}

// fillOTA aggiunge a ogni display il controllo dell'aggiornamento e l'ultimo crash.
func (s *Server) fillOTA(list []display.DisplayStatus, st *OTAStatus) {
	var pkg firmware.Info
	if s.Firmware != nil {
		pkg = s.Firmware.Read()
	} else {
		pkg.Problem = "pacchetto firmware non configurato"
	}
	for i := range list {
		d := &list[i]
		if s.Crashes != nil {
			d.LastCrash = s.Crashes.Last(d.Board)
		}
		if s.OTA != nil {
			d.OTACheck = otaCheck(d, pkg, st)
		}
	}
}

// otaCheck: si può aggiornare adesso con il pacchetto del servizio? Le condizioni di alimentazione sono
// quelle di docs/OTA.md §1.5; la scheda le ricontrolla subito prima di partire.
func otaCheck(d *display.DisplayStatus, pkg firmware.Info, st *OTAStatus) *display.OTACheck {
	no := func(why string) *display.OTACheck { return &display.OTACheck{Why: why} }
	switch {
	case d.OTA != nil && d.OTA.Active:
		return no("Aggiornamento in corso.")
	case !d.Authed:
		return no("Serve un display abbinato e autenticato: abbinalo nella pagina Dispositivo.")
	case st != nil && !st.Key:
		return no("Manca la chiave di firma sul PC: esegui «calamaio ota keygen» nel terminale.")
	case !pkg.Available:
		why := "Pacchetto firmware del servizio non disponibile"
		if pkg.Problem != "" {
			why += ": " + pkg.Problem
		}
		return no(why + ".")
	case pkg.Version == "":
		return no("Il pacchetto firmware non dice la sua versione: aggiungi un file «version» accanto a firmware.bin.")
	case !d.Status:
		return no("Il firmware " + d.FW + " di questa scheda non conosce l'aggiornamento senza cavo: la prima volta aggiornalo con il cavo USB.")
	case d.OpenPerms > 0:
		return no("C'è una richiesta di permesso aperta su questa scheda: rispondi prima, sul display o nel terminale.")
	case d.Cal:
		return no("Calibrazione della batteria in corso: aspetta che finisca.")
	}
	usb, batt := d.USB, d.Batt
	switch {
	case usb != nil && *usb:
	case batt != nil && *batt >= 50:
	case batt != nil:
		return no(fmt.Sprintf("Batteria al %d%%: collega il cavo USB, oppure ricarica sopra il 50%%.", *batt))
	case usb != nil:
		return no("Cavo USB staccato e livello della batteria sconosciuto: collega il cavo.")
	default:
		return no("La scheda non sa dire se il cavo USB è collegato né quanta batteria ha: non si aggiorna.")
	}
	ck := &display.OTACheck{Can: true}
	switch c := ota.CompareVersions(pkg.Version, d.FW); {
	case c == 0:
		ck.Warn = "La scheda ha già la versione " + d.FW + ": verrà reinstallata."
	case c < 0:
		ck.Warn = "La versione del pacchetto (" + pkg.Version + ") è più vecchia di quella della scheda (" + d.FW + ")."
	}
	return ck
}

func (s *Server) findDisplay(board string) *display.DisplayStatus {
	if s.Remote == nil {
		return nil
	}
	for _, d := range s.Remote.Status() {
		if d.Board == board {
			return &d
		}
	}
	return nil
}

func otaError(w http.ResponseWriter, err error) {
	var blk *display.BlockedError
	switch {
	case errors.Is(err, ota.ErrNoKey), errors.As(err, &blk), errors.Is(err, display.ErrOTABusy),
		errors.Is(err, display.ErrNoOTA), errors.Is(err, display.ErrOTATooLate), errors.Is(err, display.ErrOTAPerm):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	default:
		commandError(w, err)
	}
}

// apiOTAStart: POST /api/displays/{board}/ota con {} → offre il pacchetto firmware del servizio.
func (s *Server) apiOTAStart(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusConflict, errBody("aggiornamento remoto non disponibile"))
		return
	}
	board := r.PathValue("board")
	d := s.findDisplay(board)
	if d == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	if s.Firmware == nil || s.Firmware.Dir == "" {
		writeJSON(w, http.StatusConflict, errBody("pacchetto firmware non configurato (calamaio serve --firmware DIR)"))
		return
	}
	b, err := ota.Resolve(ota.ResolveOptions{Path: s.Firmware.Dir, DeviceHint: d.Device})
	if err != nil {
		writeJSON(w, http.StatusConflict, errBody("pacchetto firmware: "+err.Error()))
		return
	}
	s.archiveELF(b)
	v, err := s.OTA.StartOTA(board, display.OTAImage{Bin: b.Bin, Version: b.Version, Device: b.Device, Built: b.Built,
		Source: "pacchetto firmware del servizio (" + b.Path + ")"})
	if err != nil {
		otaError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"ota": v, "version": b.Version, "versionSource": b.VersionSrc,
		"device": b.Device, "elf": b.ELF != ""})
}

func (s *Server) archiveELF(b *ota.Build) {
	if s.ElfDir == "" || b.ELF == "" {
		return
	}
	if _, err := ota.ArchiveELF(s.ElfDir, b.ELF, b.Version, b.Info.ElfSHA256); err != nil {
		s.Log.Printf("ota: ELF non archiviato (servirebbe a tradurre i crash): %v", err)
	}
}

// apiOTAImage: POST /api/displays/{board}/ota/image?version=V&device=D[&built=MS] con l'immagine
// (application/octet-stream) → la offre. È la strada di `calamaio ota push FILE|--build DIR`.
func (s *Server) apiOTAImage(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusConflict, errBody("aggiornamento remoto non disponibile"))
		return
	}
	q := r.URL.Query()
	version, device := q.Get("version"), q.Get("device")
	built, _ := strconv.ParseInt(q.Get("built"), 10, 64)
	if built <= 0 {
		built = time.Now().UnixMilli()
	}
	bin, err := io.ReadAll(io.LimitReader(r.Body, ota.DefaultMaxApp+1))
	if err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("lettura dell'immagine non riuscita"))
		return
	}
	if len(bin) > ota.DefaultMaxApp {
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody("immagine più grande della partizione dell'app"))
		return
	}
	if _, err := ota.CheckImage(bin, version, device); err != nil {
		writeJSON(w, http.StatusBadRequest, errBody(err.Error()))
		return
	}
	src := q.Get("source")
	if src == "" || len(src) > 200 {
		src = "immagine mandata con calamaio ota push"
	}
	v, err := s.OTA.StartOTA(r.PathValue("board"), display.OTAImage{Bin: bin, Version: version, Device: device, Built: built, Source: src})
	if err != nil {
		otaError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"ota": v})
}

// apiOTACancel: POST /api/displays/{board}/ota/cancel → 202 {ota} · 409 prima di «scrivo» non c'è
// niente da annullare o è troppo tardi.
func (s *Server) apiOTACancel(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNoOTA.Error()))
		return
	}
	v, err := s.OTA.CancelOTA(r.PathValue("board"))
	if err != nil {
		otaError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"ota": v})
}

// apiLogStart: POST /api/displays/{board}/log/start {follow, minutes} → 202 {log}.
func (s *Server) apiLogStart(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusConflict, errBody("registro da remoto non disponibile"))
		return
	}
	var body struct {
		Follow  bool `json:"follow"`
		Minutes int  `json:"minutes"`
	}
	if r.ContentLength != 0 {
		dec := json.NewDecoder(io.LimitReader(r.Body, 4<<10))
		dec.DisallowUnknownFields()
		if err := dec.Decode(&body); err != nil {
			writeJSON(w, http.StatusBadRequest, errBody("serve {follow, minutes}"))
			return
		}
	}
	v, err := s.OTA.LogStart(r.PathValue("board"), body.Follow, body.Minutes)
	if err != nil {
		otaError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"log": v})
}

func (s *Server) apiLogStop(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusConflict, errBody("registro da remoto non disponibile"))
		return
	}
	v, err := s.OTA.LogStop(r.PathValue("board"))
	if err != nil {
		otaError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"log": v})
}

// apiLogRead: GET /api/displays/{board}/log?after=N[&wait=S] → {lines, next, log}. Con wait (secondi,
// al massimo 25) e nessuna riga nuova aspetta la prossima novità.
func (s *Server) apiLogRead(w http.ResponseWriter, r *http.Request) {
	if s.OTA == nil {
		writeJSON(w, http.StatusOK, map[string]any{"lines": []any{}, "next": 0, "log": nil})
		return
	}
	board := r.PathValue("board")
	if !ota.ValidBoard(board) {
		writeJSON(w, http.StatusBadRequest, errBody("scheda non valida"))
		return
	}
	after, _ := strconv.ParseInt(r.URL.Query().Get("after"), 10, 64)
	wait, _ := strconv.Atoi(r.URL.Query().Get("wait"))
	lines, next, view, wake := s.OTA.LogRead(board, after)
	if len(lines) == 0 && wait > 0 && view != nil && view.Active {
		d := min(time.Duration(wait)*time.Second, MaxOTALogWait)
		t := time.NewTimer(d)
		select {
		case <-wake:
		case <-t.C:
		case <-r.Context().Done():
		}
		t.Stop()
		lines, next, view, _ = s.OTA.LogRead(board, after)
	}
	writeJSON(w, http.StatusOK, map[string]any{"lines": lines, "next": next, "log": view})
}

// apiLogFile: GET /api/displays/{board}/log/file[?day=AAAA-MM-GG] → il file salvato (oggi se manca day).
func (s *Server) apiLogFile(w http.ResponseWriter, r *http.Request) {
	if s.LogFiles == nil {
		writeJSON(w, http.StatusNotFound, errBody("registro non salvato su questo PC"))
		return
	}
	board := r.PathValue("board")
	day := r.URL.Query().Get("day")
	if day == "" {
		day = time.Now().Format("2006-01-02")
	}
	path, err := s.LogFiles.Path(board, day)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, errBody(err.Error()))
		return
	}
	f, err := os.Open(path)
	if err != nil {
		writeJSON(w, http.StatusNotFound, errBody("nessun registro di "+board+" del "+day))
		return
	}
	defer f.Close()
	st, _ := f.Stat()
	hd := w.Header()
	hd.Set("Content-Type", "text/plain; charset=utf-8")
	hd.Set("Content-Disposition", `attachment; filename="`+board+"-"+day+`.log"`)
	hd.Set("Cache-Control", "no-store")
	http.ServeContent(w, r, "", st.ModTime(), f)
}

// apiCrashes: GET /api/crashes → {crashes: [Crash], dir} (i più recenti prima, al massimo 50).
func (s *Server) apiCrashes(w http.ResponseWriter, _ *http.Request) {
	if s.Crashes == nil {
		writeJSON(w, http.StatusOK, map[string]any{"crashes": []any{}, "dir": ""})
		return
	}
	list := s.Crashes.List(50)
	if list == nil {
		list = []*diag.Crash{}
	}
	writeJSON(w, http.StatusOK, map[string]any{"crashes": list, "dir": s.Crashes.Dir})
}

// apiCrashFile: GET /api/crashes/{id} → il rapporto leggibile (text/plain).
func (s *Server) apiCrashFile(w http.ResponseWriter, r *http.Request) {
	if s.Crashes == nil {
		writeJSON(w, http.StatusNotFound, errBody("nessun rapporto"))
		return
	}
	path, ok := s.Crashes.File(r.PathValue("id"), ".txt")
	if !ok {
		writeJSON(w, http.StatusNotFound, errBody("rapporto sconosciuto"))
		return
	}
	b, err := os.ReadFile(path)
	if err != nil {
		writeJSON(w, http.StatusNotFound, errBody("rapporto sconosciuto"))
		return
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(b)
}
