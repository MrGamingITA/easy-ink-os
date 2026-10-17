package display

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"strings"
)

// Impostazioni del display (docs/PROTOCOLLO.md, messaggi `settings` e `set`).
// Gli stessi valori del menu del firmware (core/model.h, Settings).

// SettingKeys elenca le chiavi nell'ordine del menu. workStart e workEnd (giornata di lavoro, workday.go)
// li mandano solo i firmware che gestiscono la giornata.
// game (gioco della mascotte, game.go) lo mandano solo i firmware che hanno il gioco.
var SettingKeys = []string{"homeStyle", "anim", "speed", "invert", "rot", "night", "permOn", "sound", "led", "mute", "save", "workStart", "workEnd", "game"}

var settingEnums = map[string][]string{
	"homeStyle": {"goccia", "righe", "contatori", "una"},
	"anim":      {"auto", "raffiche", "continue", "spente"},
	"speed":     {"profilo", "massima"},
	"mute":      {"off", "1h", "night"},
}

var settingBools = map[string]bool{"invert": true, "rot": true, "night": true, "permOn": true, "sound": true, "led": true, "save": true, "game": true}

// settingTimes: orari "HH:MM" (00:00–23:59) della giornata di lavoro.
var settingTimes = map[string]bool{"workStart": true, "workEnd": true}

func knownSetting(k string) bool {
	_, enum := settingEnums[k]
	return enum || settingBools[k] || settingTimes[k]
}

// MsgPermOnRemote è il rifiuto della regola §7 del brief: i permessi dal display si accendono
// solo dal dispositivo.
const MsgPermOnRemote = "Per accendere i permessi dal display usa il tasto sul dispositivo"

// InvalidError: chiave o valore non validi (HTTP 400).
type InvalidError struct{ Msg string }

func (e *InvalidError) Error() string { return e.Msg }

func invalid(format string, args ...any) error {
	return &InvalidError{Msg: fmt.Sprintf(format, args...)}
}

// parseSetting controlla una coppia chiave/valore e restituisce il valore normalizzato.
func parseSetting(key string, raw json.RawMessage) (any, error) {
	raw = bytes.TrimSpace(raw)
	if allowed, ok := settingEnums[key]; ok {
		var s string
		if len(raw) == 0 || raw[0] != '"' || json.Unmarshal(raw, &s) != nil {
			return nil, invalid("%s: serve una stringa tra %s", key, strings.Join(allowed, ", "))
		}
		for _, a := range allowed {
			if s == a {
				return s, nil
			}
		}
		return nil, invalid("%s: valore %q non ammesso (ammessi: %s)", key, s, strings.Join(allowed, ", "))
	}
	if settingBools[key] {
		// Solo true/false letterali: niente 0/1 o "true".
		switch string(raw) {
		case "true":
			return true, nil
		case "false":
			return false, nil
		}
		return nil, invalid("%s: serve true o false", key)
	}
	if settingTimes[key] {
		var t string
		if len(raw) == 0 || raw[0] != '"' || json.Unmarshal(raw, &t) != nil || !ValidClock(t) {
			return nil, invalid("%s: serve un orario \"HH:MM\" tra 00:00 e 23:59 (es. %q)", key, defaultClock(key))
		}
		return t, nil
	}
	return nil, invalid("chiave sconosciuta %q (ammesse: %s)", key, strings.Join(SettingKeys, ", "))
}

// ValidateRemote controlla le impostazioni chieste dal servizio (POST della webapp).
// permOn può solo essere spento: true viene rifiutato con MsgPermOnRemote.
func ValidateRemote(in map[string]json.RawMessage) (map[string]any, error) {
	if len(in) == 0 {
		return nil, invalid("nessuna impostazione da cambiare")
	}
	keys := make([]string, 0, len(in))
	for k := range in {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	out := make(map[string]any, len(in))
	for _, k := range keys {
		v, err := parseSetting(k, in[k])
		if err != nil {
			return nil, err
		}
		if k == "permOn" && v == true {
			return nil, &InvalidError{Msg: MsgPermOnRemote}
		}
		out[k] = v
	}
	if a, ok := out["workStart"].(string); ok {
		if b, ok := out["workEnd"].(string); ok {
			if err := CheckWorkHours(a, b); err != nil {
				return nil, err
			}
		}
	}
	return out, nil
}

// parseReported controlla le impostazioni riferite dal display. Le chiavi sconosciute
// (firmware più nuovo) si scartano; un valore sbagliato su una chiave nota scarta tutto.
func parseReported(in map[string]json.RawMessage) (map[string]any, []string, error) {
	out := make(map[string]any, len(SettingKeys))
	var unknown []string
	for k, raw := range in {
		if !knownSetting(k) {
			unknown = append(unknown, k)
			continue
		}
		v, err := parseSetting(k, raw)
		if err != nil {
			return nil, nil, err
		}
		out[k] = v
	}
	sort.Strings(unknown)
	return out, unknown, nil
}

// Errori dei comandi verso un display (HTTP 409).
var (
	ErrNotConnected = errors.New("display non collegato")
	ErrNoRemote     = errors.New("il firmware di questo display non supporta le impostazioni remote: aggiornalo")
	ErrBusy         = errors.New("il display ha troppi comandi in coda: riprova tra poco")
	ErrNotAuthed    = errors.New("display non abbinato o non autenticato: le impostazioni da remoto valgono solo per un display abbinato (calamaio pair o «Abbina» nella pagina Dispositivo)")
)
