package m5

import (
	"encoding/json"
	"errors"
	"fmt"
)

// Controlli del lato display (come firmware/src/core/link.cpp). Il servizio non li usa per sé: servono
// al display finto dei test e ai test dei vettori negativi.

// PermRequest: i campi di perm-request.
type PermRequest struct {
	ID      string `json:"id"`
	Session string `json:"session"`
	Name    string `json:"name"`
	Tool    string `json:"tool"`
	Arg     string `json:"arg"`
	At      int64  `json:"at"`
	Mac     string `json:"mac"`
}

// Verify: campi nei limiti e mac giusto con SK.
func (r PermRequest) Verify(SK []byte) error {
	switch {
	case len(r.ID) != ReqIDHex || !IsLowerHex(r.ID):
		return errors.New("id non valido")
	case len(r.Session) < 1 || len(r.Session) > SessionMax:
		return errors.New("session non valida")
	case len(r.Name) > NameMax:
		return fmt.Errorf("name oltre %d byte", NameMax)
	case len(r.Tool) < 1 || len(r.Tool) > ToolMax:
		return errors.New("tool non valido")
	case len(r.Arg) > ArgMax:
		return fmt.Errorf("arg oltre %d byte", ArgMax)
	case r.At <= 0:
		return errors.New("at mancante")
	}
	mac, ok := ParseHex(r.Mac, MacLen)
	if !ok {
		return errors.New("mac mancante o non valido")
	}
	if !Equal(mac, ReqMac(SK, r.ID, r.Session, r.Name, r.Tool, r.Arg, r.At)) {
		return errors.New("mac sbagliato")
	}
	return nil
}

// PermClose: i campi di perm-close.
type PermClose struct {
	ID     string `json:"id"`
	Reason string `json:"reason"`
	Mac    string `json:"mac"`
}

func (c PermClose) Verify(SK []byte) error {
	if len(c.ID) != ReqIDHex || !IsLowerHex(c.ID) || len(c.Reason) < 1 || len(c.Reason) > 31 {
		return errors.New("campi non validi")
	}
	mac, ok := ParseHex(c.Mac, MacLen)
	if !ok {
		return errors.New("mac mancante o non valido")
	}
	if !Equal(mac, CloseMac(SK, c.ID, c.Reason)) {
		return errors.New("mac sbagliato")
	}
	return nil
}

// CheckDisplaySide verifica un perm-request o un perm-close come farebbe il display.
func CheckDisplaySide(SK []byte, msg map[string]any) error {
	b, err := json.Marshal(msg)
	if err != nil {
		return err
	}
	switch msg["t"] {
	case "perm-request":
		var r PermRequest
		if err := json.Unmarshal(b, &r); err != nil {
			return err
		}
		return r.Verify(SK)
	case "perm-close":
		var c PermClose
		if err := json.Unmarshal(b, &c); err != nil {
			return err
		}
		return c.Verify(SK)
	}
	return fmt.Errorf("tipo %v non verificabile", msg["t"])
}
