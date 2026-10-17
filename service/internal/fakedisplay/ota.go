package fakedisplay

import (
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"

	"calamaio/internal/m5"
	"calamaio/internal/ota"
)

// La parte del display dell'aggiornamento remoto (docs/OTA.md §2-§3) e del registro da remoto (§5),
// come la deve fare il firmware: ota-key accettata solo con il mac giusto e solo se non contraddice la
// chiave salvata; ota-offer controllata (autenticazione, mac, firma del manifesto, board e device,
// cavo o batteria ≥ 50%), poi download HTTP con SHA-256 al volo, stati ricevo/verifico/scrivo/riavvio.

// OTAOptions: comportamento del display finto.
type OTAOptions struct {
	Batt       *int          // batteria misurata (nil = non la sa)
	USB        *bool         // cavo USB (nil = non lo sa)
	Manual     bool          // non scarica da solo: il test chiama Accept
	ChunkDelay time.Duration // pausa tra un pezzo e l'altro del download (per annullare a metà)
	NoReboot   bool          // dopo il download si ferma in «scrivo» (per provare l'annullamento tardivo)
}

type otaState struct {
	mu     sync.Mutex
	fw     string
	device string
	opt    OTAOptions
	pub    []byte // chiave pubblica di firma salvata
	offer  map[string]any
	cancel chan struct{}
	image  []byte // immagine «scritta» (dopo riavvio)
	ring   []string
}

// FW: versione del firmware finto.
func (d *Display) FW() string { return d.ota.fw }

// OtaPub: chiave pubblica di firma salvata (nil se nessuna).
func (d *Display) OtaPub() []byte {
	d.ota.mu.Lock()
	defer d.ota.mu.Unlock()
	return append([]byte(nil), d.ota.pub...)
}

// Image: l'immagine scritta dall'ultimo aggiornamento.
func (d *Display) Image() []byte {
	d.ota.mu.Lock()
	defer d.ota.mu.Unlock()
	return d.ota.image
}

// SetPower cambia cavo e batteria e manda `status`.
func (d *Display) SetPower(batt *int, usb *bool) {
	d.ota.mu.Lock()
	d.ota.opt.Batt, d.ota.opt.USB = batt, usb
	d.ota.mu.Unlock()
	d.sendJSON(map[string]any{"t": "status", "batt": batt, "usb": usb})
}

// LogRing: righe del registro in RAM (le manda con log-start).
func (d *Display) LogRing(lines ...string) {
	d.ota.mu.Lock()
	d.ota.ring = append(d.ota.ring, lines...)
	d.ota.mu.Unlock()
}

// SendLog manda righe nuove per la sessione id (con follow).
func (d *Display) SendLog(id string, lines ...string) {
	d.sendJSON(map[string]any{"t": "log", "id": id, "lines": lines})
}

// SendCrash manda un rapporto di crash.
func (d *Display) SendCrash(m map[string]any) {
	m["t"] = "crash"
	d.sendJSON(m)
}

// SendOtaStatus manda un ota-status a mano.
func (d *Display) SendOtaStatus(id, phase string, done, total int64, why string) {
	m := map[string]any{"t": "ota-status", "id": id, "phase": phase, "done": done, "total": total}
	if why != "" {
		m["error"] = why
	}
	d.sendJSON(m)
}

func (d *Display) rejectOta(id, why string, m map[string]any) {
	d.SendOtaStatus(id, "rifiutato", 0, 0, why)
	d.emit(Event{Type: "ota-rejected", Text: why, Msg: m})
}

func (d *Display) handleOTA(t string, m map[string]any, raw []byte) bool {
	switch t {
	case "ota-key":
		d.mu.Lock()
		authed, sk := d.authed, d.sk
		d.mu.Unlock()
		pub, ok := m5.ParseHex(str(m, "pub"), 32)
		mac, ok2 := m5.ParseHex(str(m, "mac"), m5.MacLen)
		switch {
		case !authed:
			d.emit(Event{Type: "rejected", Text: "ota-key senza autenticazione", Msg: m})
		case !ok || !ok2 || !m5.Equal(mac, m5.OtaKeyMac(sk, str(m, "pub"))):
			d.emit(Event{Type: "rejected", Text: "ota-key con un mac sbagliato", Msg: m})
		default:
			d.ota.mu.Lock()
			old := d.ota.pub
			if old != nil && !m5.Equal(old, pub) {
				d.ota.mu.Unlock()
				d.emit(Event{Type: "rejected", Text: "ota-key diversa da quella salvata: serve un nuovo abbinamento", Msg: m})
				return true
			}
			d.ota.pub = pub
			d.ota.mu.Unlock()
			d.emit(Event{Type: t, Msg: m})
		}
		return true
	case "ota-offer":
		d.onOffer(m, raw)
		return true
	case "ota-cancel":
		d.mu.Lock()
		authed, sk := d.authed, d.sk
		d.mu.Unlock()
		mac, ok := m5.ParseHex(str(m, "mac"), m5.MacLen)
		if !authed || !ok || !m5.Equal(mac, m5.OtaCancelMac(sk, str(m, "id"))) {
			d.emit(Event{Type: "rejected", Text: "ota-cancel non valido", Msg: m})
			return true
		}
		d.ota.mu.Lock()
		if d.ota.cancel != nil {
			close(d.ota.cancel)
			d.ota.cancel = nil
		}
		d.ota.mu.Unlock()
		d.emit(Event{Type: t, Msg: m})
		return true
	case "log-start":
		d.mu.Lock()
		authed := d.authed
		d.mu.Unlock()
		if !authed {
			d.emit(Event{Type: "rejected", Text: "log-start senza autenticazione", Msg: m})
			return true
		}
		d.ota.mu.Lock()
		ring := append([]string(nil), d.ota.ring...)
		d.ota.mu.Unlock()
		id := str(m, "id")
		d.emit(Event{Type: t, Text: id, Msg: m})
		// il contenuto dell'anello in messaggi da al massimo 3 righe (nei test), l'ultimo con end
		for i := 0; i < len(ring) || i == 0; i += 3 {
			part := ring[i:min(i+3, len(ring))]
			msg := map[string]any{"t": "log", "id": id, "lines": part}
			if i+3 >= len(ring) {
				msg["end"] = true
			}
			d.sendJSON(msg)
		}
		return true
	case "log-stop":
		d.emit(Event{Type: t, Text: str(m, "id"), Msg: m})
		return true
	}
	return false
}

func (d *Display) onOffer(m map[string]any, raw []byte) {
	var off struct {
		ID       string       `json:"id"`
		Manifest ota.Manifest `json:"manifest"`
		Sig      string       `json:"sig"`
		URL      string       `json:"url"`
		Mac      string       `json:"mac"`
	}
	_ = json.Unmarshal(raw, &off)
	d.mu.Lock()
	authed, sk := d.authed, d.sk
	d.mu.Unlock()
	d.ota.mu.Lock()
	pub, opt, device := d.ota.pub, d.ota.opt, d.ota.device
	d.ota.mu.Unlock()
	mac, okMac := m5.ParseHex(off.Mac, m5.MacLen)
	switch {
	case !authed:
		d.emit(Event{Type: "rejected", Text: "ota-offer senza autenticazione", Msg: m})
		return // senza SK non può nemmeno rispondere in modo credibile
	case !okMac || !m5.Equal(mac, m5.OtaOfferMac(sk, off.ID, off.Manifest.SHA256)):
		d.rejectOta(off.ID, "offerta non autentica (mac sbagliato)", m)
		return
	case pub == nil:
		d.rejectOta(off.ID, "nessuna chiave di firma: riabbina la scheda", m)
		return
	case !off.Manifest.Verify(ed25519.PublicKey(pub), off.Sig):
		d.rejectOta(off.ID, "firma non valida", m)
		return
	case off.Manifest.Board != d.Board:
		d.rejectOta(off.ID, "immagine per un'altra scheda ("+off.Manifest.Board+")", m)
		return
	case off.Manifest.Device != device:
		d.rejectOta(off.ID, "immagine per un altro modello ("+off.Manifest.Device+")", m)
		return
	}
	switch {
	case opt.USB != nil && *opt.USB:
	case opt.Batt != nil && *opt.Batt >= 50:
	case opt.Batt != nil:
		d.rejectOta(off.ID, fmt.Sprintf("batteria al %d%%: collega il cavo USB", *opt.Batt), m)
		return
	default:
		d.rejectOta(off.ID, "non so se c'è il cavo né quanta batteria resta", m)
		return
	}
	d.ota.mu.Lock()
	d.ota.offer = m
	d.ota.cancel = make(chan struct{})
	d.ota.mu.Unlock()
	d.emit(Event{Type: "ota-offer", Msg: m})
	if !opt.Manual {
		go d.Download(off.ID, off.URL, off.Manifest)
	}
}

// Download scarica l'immagine come la scheda: GET, SHA-256 al volo, ota-status ogni 25%, verifica,
// scrittura, riavvio (la connessione si chiude). Restituisce l'esito.
func (d *Display) Download(id, url string, man ota.Manifest) error {
	d.ota.mu.Lock()
	cancel, opt := d.ota.cancel, d.ota.opt
	d.ota.mu.Unlock()
	fail := func(why string) error {
		d.SendOtaStatus(id, "errore", 0, man.Size, why)
		d.emit(Event{Type: "ota-error", Text: why})
		return fmt.Errorf("%s", why)
	}
	d.SendOtaStatus(id, "ricevo", 0, man.Size, "")
	resp, err := http.Get(url)
	if err != nil {
		return fail("download non riuscito: " + err.Error())
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fail(fmt.Sprintf("download rifiutato dal PC (HTTP %d)", resp.StatusCode))
	}
	if resp.ContentLength != man.Size {
		return fail(fmt.Sprintf("dimensione annunciata %d invece di %d", resp.ContentLength, man.Size))
	}
	h := sha256.New()
	var img []byte
	buf := make([]byte, 64<<10)
	next := man.Size / 4
	for {
		if cancel != nil {
			select {
			case <-cancel:
				return fail("annullato dal PC")
			default:
			}
		}
		n, err := resp.Body.Read(buf)
		if n > 0 {
			h.Write(buf[:n])
			img = append(img, buf[:n]...)
			if int64(len(img)) >= next && int64(len(img)) < man.Size {
				d.SendOtaStatus(id, "ricevo", int64(len(img)), man.Size, "")
				next += man.Size / 4
			}
			if opt.ChunkDelay > 0 {
				time.Sleep(opt.ChunkDelay)
			}
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			return fail("download interrotto: " + err.Error())
		}
	}
	d.SendOtaStatus(id, "ricevo", int64(len(img)), man.Size, "")
	d.SendOtaStatus(id, "verifico", int64(len(img)), man.Size, "")
	if int64(len(img)) != man.Size || hex.EncodeToString(h.Sum(nil)) != man.SHA256 {
		return fail("verifica non riuscita: l'immagine non corrisponde al manifesto")
	}
	if cancel != nil {
		select {
		case <-cancel:
			return fail("annullato dal PC")
		default:
		}
	}
	d.SendOtaStatus(id, "scrivo", int64(len(img)), man.Size, "")
	if opt.NoReboot {
		d.emit(Event{Type: "ota-written"})
		return nil
	}
	d.SendOtaStatus(id, "riavvio", int64(len(img)), man.Size, "")
	d.ota.mu.Lock()
	d.ota.image = img
	d.ota.mu.Unlock()
	d.emit(Event{Type: "ota-reboot"})
	time.Sleep(50 * time.Millisecond)
	d.Close() // riavvio
	return nil
}
