package avatar

import "calamaio/internal/state"

// SessionAvatar: state.AvatarResolver per lo Store (avatar di una sessione nei messaggi state).
func (m *Manager) SessionAvatar(id, cwd string) state.AvatarRef {
	r := m.Resolve(id, cwd)
	if r.Entry == nil {
		return state.AvatarRef{Project: r.Project}
	}
	return state.AvatarRef{ID: r.Entry.ID, CRC: r.Entry.CRC, From: r.From, Project: r.Project}
}

// DefaultAvatar: state.AvatarResolver, l'avatar predefinito.
func (m *Manager) DefaultAvatar() state.AvatarRef {
	e := m.DefaultEntry()
	if e == nil {
		return state.AvatarRef{}
	}
	return state.AvatarRef{ID: e.ID, CRC: e.CRC, From: FromDefault}
}

// AvatarPacket: il binario CAVT di un avatar per il messaggio `avatar` (display.Hub).
func (m *Manager) AvatarPacket(id string) (uint32, []byte, bool) {
	e := m.Lib.Get(id)
	if e == nil {
		return 0, nil, false
	}
	return e.CRC, e.Bin, true
}

// LegacyAvatarPacket: il CAVT senza la posa run per il firmware ≤ 0.6.0, che rifiuta il bit 9 di
// poseMask (docs/AVATAR.md §12.4).
func (m *Manager) LegacyAvatarPacket(id string) (uint32, []byte, bool) {
	e := m.Lib.Get(id)
	if e == nil {
		return 0, nil, false
	}
	return e.CRCLegacy, e.BinLegacy, true
}

// AnchorsPacket: il blocco CANC delle ancore di id (docs/AVATAR.md §13.4) e il CRC del CAVT a cui è
// legato; false se l'avatar non c'è o non ha ancore (niente accessori su di lui).
func (m *Manager) AnchorsPacket(id string) (uint32, []byte, bool) {
	e := m.Lib.Get(id)
	if e == nil || e.Anchors == nil {
		return 0, nil, false
	}
	return e.CRC, e.Anchors, true
}
