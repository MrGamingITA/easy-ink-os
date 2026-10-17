package display

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"regexp"
	"time"

	"calamaio/internal/state"
)

// Avatar delle sessioni (docs/AVATAR.md §10, docs/PROTOCOLLO.md «Avatar»): lo `state` porta solo
// id e CRC; il display chiede i binari che non ha con `avatar-need` e il servizio risponde con un
// messaggio `avatar` per ogni id. Gli avatar non passano mai dentro `state`.

// AvatarSource: libreria degli avatar (avatar.Manager). nil = nessun avatar (welcome senza
// predefinito, avatar-need ignorati).
type AvatarSource interface {
	DefaultAvatar() state.AvatarRef
	AvatarPacket(id string) (crc uint32, bin []byte, ok bool)
}

// MaxFrame: il display accetta frame fino a 15 KB (firmware: kMaxFrame in core/apply_state.h).
const MaxFrame = 15 * 1024

// MaxAvatarNeed: id al massimo in un avatar-need (lo schema dice lo stesso).
const MaxAvatarNeed = 16

// avatarResend: lo stesso avatar (id e CRC) non si rimanda alla stessa connessione prima di questo
// tempo, anche se il display lo richiede (difesa da un display che chiede in un ciclo).
const avatarResend = 3 * time.Second

var avatarIDRe = regexp.MustCompile(`^[a-z0-9]+(-[a-z0-9]+)*$`)

// AvatarMsg: servizio → display, il binario CAVT in base64.
type AvatarMsg struct {
	T    string `json:"t"`
	V    int    `json:"v"`
	ID   string `json:"id"`
	CRC  uint32 `json:"crc"`
	Data string `json:"data"`
}

type avatarNeedMsg struct {
	IDs []string `json:"ids"`
}

// parseAvatarNeed: ids validi (1..16, kebab-case, al massimo 24 caratteri), senza doppioni.
func parseAvatarNeed(data []byte) ([]string, bool) {
	var m avatarNeedMsg
	if err := json.Unmarshal(data, &m); err != nil || len(m.IDs) == 0 || len(m.IDs) > MaxAvatarNeed {
		return nil, false
	}
	seen := map[string]bool{}
	out := make([]string, 0, len(m.IDs))
	for _, id := range m.IDs {
		if len(id) > 24 || !avatarIDRe.MatchString(id) {
			return nil, false
		}
		if !seen[id] {
			seen[id] = true
			out = append(out, id)
		}
	}
	return out, true
}

// LegacyAvatarSource: il CAVT senza la posa run per i firmware che non la conoscono (avatar.Manager).
type LegacyAvatarSource interface {
	LegacyAvatarPacket(id string) (crc uint32, bin []byte, ok bool)
}

// AvatarMessage costruisce il messaggio `avatar` (false se l'id è sconosciuto o il frame sarebbe
// oltre MaxFrame).
func AvatarMessage(src AvatarSource, id string) (AvatarMsg, bool) {
	return avatarMessage(src, id, false)
}

// avatarMessage: con legacy il CAVT senza run (firmware ≤ 0.6.0, docs/AVATAR.md §12.4).
func avatarMessage(src AvatarSource, id string, legacy bool) (AvatarMsg, bool) {
	crc, bin, ok := avatarPacket(src, id, legacy)
	if !ok {
		return AvatarMsg{}, false
	}
	m := AvatarMsg{T: "avatar", V: ProtocolVersion, ID: id, CRC: crc, Data: base64.StdEncoding.EncodeToString(bin)}
	if b, err := json.Marshal(m); err != nil || len(b) > MaxFrame {
		return AvatarMsg{}, false
	}
	return m, true
}

// sendAvatars risponde a un avatar-need (chiamata da run): `avatar-offer` in coda per le schede con avatar-dl
// (avatar_dl.go), il messaggio `avatar` per le altre. Mai un avatar fallito da poco su questa scheda (avatarHeld).
func (h *Hub) sendAvatars(ctx context.Context, c *conn, ids []string, sent map[string]avatarSent) error {
	for _, id := range ids {
		crc, bin, ok := avatarPacket(h.Avatars, id, c.legacyAv)
		if !ok {
			h.Log.Printf("ws: #%d avatar-need: %q sconosciuto (il display usa il predefinito)", c.info.ID, id)
			continue
		}
		if s, ok := sent[id]; ok && s.crc == crc && h.Now().Sub(s.at) < avatarResend {
			continue
		}
		h.mu.Lock()
		held, dl := h.avatarHeld(c.info.Board, id, crc), c.avatarDl
		if !held && dl {
			h.avatarQueue(c, avatarItem{id: id, crc: crc, bin: bin})
		}
		h.mu.Unlock()
		if held {
			h.Log.Printf("ws: #%d avatar %s non rimandato: consegna fallita da meno di %s", c.info.ID, id, h.AvatarFailHold)
			continue
		}
		sent[id] = avatarSent{crc: crc, at: h.Now()}
		if dl {
			continue
		}
		m, ok := avatarMessage(h.Avatars, id, c.legacyAv)
		if !ok {
			continue
		}
		// segnato prima della write: se la connessione cade a metà frame conta lo stesso
		h.mu.Lock()
		c.avLegacy = append(c.avLegacy, avatarLegacySent{id: id, crc: m.CRC, at: h.Now()})
		h.mu.Unlock()
		if err := h.write(ctx, c, m); err != nil {
			return err
		}
		h.Log.Printf("ws: #%d avatar %s mandato (crc %08x, %d caratteri)", c.info.ID, id, m.CRC, len(m.Data))
	}
	return nil
}

// avatarPacket: CRC e binario di id (senza la posa run con legacy).
func avatarPacket(src AvatarSource, id string, legacy bool) (uint32, []byte, bool) {
	if src == nil {
		return 0, nil, false
	}
	if l, isL := src.(LegacyAvatarSource); legacy && isL {
		return l.LegacyAvatarPacket(id)
	}
	return src.AvatarPacket(id)
}

type avatarSent struct {
	crc uint32
	at  time.Time
}

// welcomeAvatar: avatar predefinito per welcome (facoltativo).
func (h *Hub) welcomeAvatar(w *Welcome) {
	if h.Avatars == nil {
		return
	}
	if d := h.Avatars.DefaultAvatar(); d.ID != "" {
		crc := d.CRC
		w.DefaultAvatar, w.DefaultAvatarCrc = d.ID, &crc
	}
}

// legacyCRC: il CRC del CAVT senza run di id (crc se non si sa).
func (h *Hub) legacyCRC(id string, crc uint32) uint32 {
	if l, ok := h.Avatars.(LegacyAvatarSource); ok {
		if lc, _, found := l.LegacyAvatarPacket(id); found {
			return lc
		}
	}
	return crc
}

// legacyCRCs riscrive i CRC degli avatar di un messaggio per un firmware senza la posa run: lo state e
// il welcome devono annunciare lo stesso CAVT che poi il messaggio `avatar` gli manda, altrimenti la
// scheda lo richiederebbe in un ciclo.
func (h *Hub) legacyState(m state.Message) state.Message {
	if h.Avatars == nil {
		return m
	}
	ss := make([]state.Session, len(m.Sessions))
	copy(ss, m.Sessions)
	for i := range ss {
		if ss[i].Avatar != "" && ss[i].AvatarCrc != nil {
			c := h.legacyCRC(ss[i].Avatar, *ss[i].AvatarCrc)
			ss[i].AvatarCrc = &c
		}
	}
	m.Sessions = ss
	if m.DefaultAvatar != "" && m.DefaultAvatarCrc != nil {
		c := h.legacyCRC(m.DefaultAvatar, *m.DefaultAvatarCrc)
		m.DefaultAvatarCrc = &c
	}
	return m
}
