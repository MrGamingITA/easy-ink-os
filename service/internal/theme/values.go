package theme

// Valori nei testi (docs/TEMI.md §3). ID: sessione 0x00–0x09, riga 0x40–0x49, generali 0x80–0x95.
// 0x81 (ora con i secondi) lo produce solo l'orologio `hms`: non ha un nome.
type ValueSpec struct {
	Name    string
	ID      uint16
	Row     bool
	Example string
	Glyphs  string // alfabeto chiuso, in ordine di codepoint
	Free    bool   // testo libero
}

const ValClockSec uint16 = 0x81

// glyphs vuoto = testo libero.
var sessionNames = []struct{ name, example, glyphs string }{
	{"nome", "api-server", ""}, {"stato", "Modifico src/api.ts", ""}, {"attività", "Bash: npm run test", ""}, {"riassunto", "Guida aggiornata", ""},
	{"meta", "3/7 task  ·  12 min", ""}, {"contesto", "42%", alpha("0123456789%")}, {"task", "3/7", alpha("0123456789/")},
	{"da", "12 min", alpha("0123456789 min h")}, {"sub", "2", alpha("0123456789")}, {"test", "✓", "✓✗"},
}

var generalNames = []struct {
	name    string
	id      uint16
	example string
	glyphs  string // vuoto = testo libero
}{
	{"ora", 0x80, "10:30", ClockGlyphs}, {"data", 0x82, "domenica 11 ottobre", alpha(weekdays + months + "0123456789 ")},
	{"giorno", 0x83, "domenica", alpha(weekdays)}, {"sessioni", 0x84, "3", alpha("0123456789")},
	{"inattesa", 0x85, "1", alpha("0123456789")}, {"batteria", 0x86, "78%", alpha("0123456789%")},
	{"autonomia", 0x87, "Autonomia: circa 3 giorni con il tuo uso",
		alpha("Autonomia: circa giorni ore con il tuo uso (dalla scarica) calibra per una stima in corso 0123456789")},
	{"wifi", 0x88, "CasaWiFi", ""}, {"pc", 0x89, "calamaio.local", ""},
	{"giornata.mancano", 0x8A, "6 h 25", alpha("0123456789 h min finita")}, {"giornata.fine", 0x8B, "18:00", alpha("0123456789:")},
	{"oggi.task", 0x8C, "4", alpha("0123456789")}, {"oggi.righe", 0x8D, "+120 −30", alpha("0123456789+−- ")},
	{"oggi.costo", 0x8E, "1,20 $", alpha("0123456789,- $")}, {"limiti.5h", 0x8F, "37%", alpha("0123456789%—")},
	{"limiti.7g", 0x90, "12%", alpha("0123456789%—")}, {"mascotte.livello", 0x91, "3", alpha("0123456789—")},
	{"mascotte.xp", 0x92, "66/417 XP", alpha("0123456789/- XP")}, {"mascotte.energia", 0x93, "80%", alpha("0123456789%-")},
	{"co2", 0x94, "620 ppm", alpha("0123456789 ppm")}, {"versione", 0x95, "0.13.0-ed", ""},
}

var Values = func() []ValueSpec {
	var v []ValueSpec
	for i, s := range sessionNames {
		v = append(v, ValueSpec{"sessione." + s.name, uint16(i), false, s.example, s.glyphs, s.glyphs == ""})
		v = append(v, ValueSpec{"riga." + s.name, uint16(0x40 + i), true, s.example, s.glyphs, s.glyphs == ""})
	}
	for _, g := range generalNames {
		v = append(v, ValueSpec{g.name, g.id, false, g.example, g.glyphs, g.glyphs == ""})
	}
	return v
}()

func ValueID(name string) (uint16, bool) {
	for _, v := range Values {
		if v.Name == name {
			return v.ID, true
		}
	}
	return 0, false
}
