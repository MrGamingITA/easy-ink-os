package display

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"

	"calamaio/internal/gamesvc"
)

// Gioco della mascotte (docs/PROTOCOLLO.md «Gioco della mascotte»): il servizio manda `game` dopo
// hello, dopo auth-ok e a ogni cambio; il display chiede i disegni che non ha con `accessory-need`
// (binario CACC) e `anchors-need` (blocco CANC delle ancore di un avatar), e sceglie l'accessorio
// dal menu con `accessory-wear`. Tutti facoltativi: un firmware che non li conosce li ignora.

// GameSource: il gioco (gamesvc.Service). nil = niente messaggi del gioco.
type GameSource interface {
	Message() gamesvc.Message
	Subscribe() (<-chan gamesvc.Message, func())
	Wear(id string) error
	SetOn(on bool)
	On() bool
	AccessoryPacket(id string) (crc uint32, bin []byte, ok bool)
}

// AnchorSource: le ancore degli avatar (avatar.Manager).
type AnchorSource interface {
	AnchorsPacket(id string) (avatarCRC uint32, bin []byte, ok bool)
}

// MaxAnchorsNeed: avatar al massimo in un anchors-need.
const MaxAnchorsNeed = 16

// AccessoryMsg: servizio → display, il binario CACC in base64.
type AccessoryMsg struct {
	T    string `json:"t"`
	V    int    `json:"v"`
	ID   string `json:"id"`
	CRC  uint32 `json:"crc"`
	Data string `json:"data"`
}

// AnchorsMsg: servizio → display, il blocco CANC delle ancore di un avatar in base64.
type AnchorsMsg struct {
	T         string `json:"t"`
	V         int    `json:"v"`
	Avatar    string `json:"avatar"`
	AvatarCrc uint32 `json:"avatarCrc"`
	Data      string `json:"data"`
}

// AvatarVersion: un avatar con il CRC del CAVT che il display ha in cache (anchors-need).
type AvatarVersion struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

type wearMsg struct {
	ID string // "" = nessuno
}

func parseAnchorsNeed(data []byte) ([]AvatarVersion, bool) {
	var m struct {
		Avatars []struct {
			ID  string           `json:"id"`
			CRC *json.RawMessage `json:"crc"`
		} `json:"avatars"`
	}
	if err := json.Unmarshal(data, &m); err != nil || len(m.Avatars) == 0 || len(m.Avatars) > MaxAnchorsNeed {
		return nil, false
	}
	seen := map[string]bool{}
	var out []AvatarVersion
	for _, a := range m.Avatars {
		var crc uint32
		if len(a.ID) > 24 || !avatarIDRe.MatchString(a.ID) || a.CRC == nil || json.Unmarshal(*a.CRC, &crc) != nil {
			return nil, false
		}
		if !seen[a.ID] {
			seen[a.ID] = true
			out = append(out, AvatarVersion{ID: a.ID, CRC: crc})
		}
	}
	return out, true
}

// parseWear: {"id": "<accessorio>"} oppure {"id": null}; il campo è obbligatorio.
func parseWear(data []byte) (*wearMsg, bool) {
	var m map[string]json.RawMessage
	if json.Unmarshal(data, &m) != nil {
		return nil, false
	}
	raw, ok := m["id"]
	if !ok {
		return nil, false
	}
	if string(bytes.TrimSpace(raw)) == "null" {
		return &wearMsg{}, true
	}
	var id string
	if json.Unmarshal(raw, &id) != nil || len(id) > 24 || !avatarIDRe.MatchString(id) {
		return nil, false
	}
	return &wearMsg{ID: id}, true
}

// sendGame: il messaggio game di adesso (dopo hello).
func (h *Hub) sendGame(ctx context.Context, c *conn) error {
	if h.Game == nil {
		return nil
	}
	return h.write(ctx, c, h.Game.Message())
}

// queueGame mette in coda il messaggio game (dopo auth-ok, a ogni cambio). Coda piena: si salta,
// il prossimo porta lo stato intero.
func (h *Hub) queueGame(c *conn, m gamesvc.Message) {
	select {
	case c.out <- m:
	default:
		h.Log.Printf("ws: #%d coda piena: game saltato", c.info.ID)
	}
}

// RunGame manda `game` a tutti i display collegati a ogni cambio del gioco, finché done non si chiude.
func (h *Hub) RunGame(done <-chan struct{}) {
	if h.Game == nil {
		return
	}
	ch, cancel := h.Game.Subscribe()
	defer cancel()
	for {
		select {
		case <-done:
			return
		case m := <-ch:
			h.mu.Lock()
			if h.farewell || h.closed {
				h.mu.Unlock()
				continue
			}
			for _, c := range h.conns {
				if c.info.Hello {
					h.queueGame(c, m)
				}
			}
			h.mu.Unlock()
			if m.Celebrate != nil {
				h.Log.Printf("ws: festa «%s» ai display", m.Celebrate.Title)
			}
		}
	}
}

// sendAccessories risponde a un accessory-need.
func (h *Hub) sendAccessories(ctx context.Context, c *conn, ids []string, sent map[string]avatarSent) error {
	for _, id := range ids {
		if h.Game == nil {
			return nil
		}
		crc, bin, ok := h.Game.AccessoryPacket(id)
		if !ok {
			h.Log.Printf("ws: #%d accessory-need: %q sconosciuto", c.info.ID, id)
			continue
		}
		if s, ok := sent[id]; ok && s.crc == crc && h.Now().Sub(s.at) < avatarResend {
			continue
		}
		m := AccessoryMsg{T: "accessory", V: ProtocolVersion, ID: id, CRC: crc, Data: base64.StdEncoding.EncodeToString(bin)}
		if err := h.write(ctx, c, m); err != nil {
			return err
		}
		sent[id] = avatarSent{crc: crc, at: h.Now()}
		h.Log.Printf("ws: #%d accessorio %s mandato (crc %08x, %d byte)", c.info.ID, id, crc, len(bin))
	}
	return nil
}

// sendAnchors risponde a un anchors-need: solo per lo stesso disegno (CRC del CAVT) che ha il
// display; per un disegno diverso le ancore sarebbero sbagliate, e il display chiederà di nuovo
// dopo aver ricevuto l'avatar nuovo.
func (h *Hub) sendAnchors(ctx context.Context, c *conn, avs []AvatarVersion, sent map[string]avatarSent) error {
	src, ok := h.Avatars.(AnchorSource)
	if !ok {
		return nil
	}
	for _, av := range avs {
		crc, bin, ok := src.AnchorsPacket(av.ID)
		switch {
		case !ok:
			h.Log.Printf("ws: #%d anchors-need: %q senza ancore (niente accessori su di lui)", c.info.ID, av.ID)
			continue
		case crc != av.CRC:
			h.Log.Printf("ws: #%d anchors-need: %s %08x, il servizio ha le ancore per %08x: prima l'avatar nuovo", c.info.ID, av.ID, av.CRC, crc)
			continue
		}
		if s, ok := sent[av.ID]; ok && s.crc == crc && h.Now().Sub(s.at) < avatarResend {
			continue
		}
		m := AnchorsMsg{T: "anchors", V: ProtocolVersion, Avatar: av.ID, AvatarCrc: crc, Data: base64.StdEncoding.EncodeToString(bin)}
		if err := h.write(ctx, c, m); err != nil {
			return err
		}
		sent[av.ID] = avatarSent{crc: crc, at: h.Now()}
		h.Log.Printf("ws: #%d ancore di %s mandate (%d byte)", c.info.ID, av.ID, len(bin))
	}
	return nil
}

// onWear: scelta dell'accessorio dal menu della scheda. Il gioco manda `game` a tutti se cambia;
// se la scelta non è valida (bloccato, sconosciuto) la scheda riceve comunque `game` con quello vero.
func (h *Hub) onWear(c *conn, m *wearMsg) {
	if h.Game == nil {
		return
	}
	if err := h.Game.Wear(m.ID); err != nil {
		h.Log.Printf("ws: #%d accessory-wear %q rifiutato: %v", c.info.ID, m.ID, err)
		h.mu.Lock()
		h.queueGame(c, h.Game.Message())
		h.mu.Unlock()
		return
	}
	h.Log.Printf("ws: #%d accessory-wear %q", c.info.ID, m.ID)
}

// gameSetting: `settings.game` cambiato sulla scheda (menu › Display › Gioco della mascotte) rispetto
// al valore riferito prima sulla stessa connessione: il servizio accende o spegne il gioco. Il primo
// `settings` di una connessione non cambia niente: vale game.on del servizio, e la scheda si allinea.
func (h *Hub) gameSetting(c *conn, prev, next map[string]any) {
	if h.Game == nil {
		return
	}
	nv, ok := next["game"].(bool)
	if !ok {
		return
	}
	pv, had := prev["game"].(bool)
	if !had {
		if nv != h.Game.On() {
			h.Log.Printf("ws: #%d settings.game=%v, il servizio ha il gioco %v: vale il servizio (game.on)", c.info.ID, nv, !nv)
		}
		return
	}
	if pv != nv {
		h.Log.Printf("ws: #%d gioco della mascotte %v dal menu della scheda", c.info.ID, nv)
		h.Game.SetOn(nv)
	}
}
