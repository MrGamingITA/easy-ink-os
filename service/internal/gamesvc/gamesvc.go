// Package gamesvc mette il motore del gioco della mascotte (internal/game, puro) dentro il servizio:
// salvataggio atomico in <dati>/calamaio/game.json, storico degli XP per giorno, catalogo dalla
// libreria degli accessori, messaggio `game` per i display, proposta dell'accessorio speciale a Claude.
// Specifica: docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md; messaggi: docs/PROTOCOLLO.md.
package gamesvc

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"slices"
	"sync"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/game"
)

// FileFormat: game.json.
const FileFormat = "calamaio-game"

// HistoryDays: giorni di storico degli XP conservati.
const HistoryDays = 365

// MaxUnlocked: voci di `unlocked` nel messaggio game (il menu della scheda).
const MaxUnlocked = 20

// Risposte alla proposta dell'accessorio speciale.
const (
	AnswerLater   = "later"
	AnswerDecline = "decline"
)

// Proposal: la proposta dell'accessorio speciale a Claude (specifica §4). Una per traguardo; «più
// tardi» (o nessuna risposta) la ripropone al primo prompt di un altro giorno; «no» la chiude.
type Proposal struct {
	For    string `json:"for,omitempty"`    // traguardo (SpecialPending) a cui si riferisce
	Asked  string `json:"asked,omitempty"`  // giorno (AAAA-MM-GG) dell'ultima proposta mandata a Claude
	Answer string `json:"answer,omitempty"` // later, oppure vuoto
}

type fileData struct {
	Format   string         `json:"format"`
	Version  int            `json:"version"`
	State    game.State     `json:"state"`
	History  map[string]int `json:"history"` // giorno → XP guadagnati
	Proposal Proposal       `json:"proposal"`
}

// Ref: accessorio indossato nel messaggio game.
type Ref struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

// Celebrate: festa sulla scheda (solo nel messaggio del momento).
type Celebrate struct {
	ID    string `json:"id"`
	Title string `json:"title"`
}

// Item: accessorio sbloccato per il menu della scheda.
type Item struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// Message: servizio → display, `game` (docs/PROTOCOLLO.md «Gioco della mascotte»).
type Message struct {
	T            string     `json:"t"`
	V            int        `json:"v"`
	On           bool       `json:"on"`
	Level        int        `json:"level"`
	XP           int        `json:"xp"`
	XPLevelStart int        `json:"xpLevelStart"`
	XPNext       int        `json:"xpNext"`
	Energy       int        `json:"energy"`
	Tired        bool       `json:"tired"`
	Wear         *Ref       `json:"wear"`
	Celebrate    *Celebrate `json:"celebrate,omitempty"`
	Unlocked     []Item     `json:"unlocked"`
	// Special: c'è un accessorio speciale da creare (SpecialPending, proposta non chiusa con «No»):
	// la scheda mostra il toast «Nuovo accessorio speciale: chiedilo a Claude». Assente altrimenti.
	Special bool `json:"special,omitempty"`
}

// Service: il gioco nel servizio. Sicuro per l'uso concorrente.
type Service struct {
	Path string // game.json ("" = solo in memoria)
	Lib  *accessory.Library
	Log  *log.Logger
	Now  func() time.Time

	mu       sync.Mutex
	eng      *game.Engine
	libRev   uint64
	history  map[string]int
	proposal Proposal
	dirty    bool
	last     []byte // ultimo messaggio mandato (senza celebrate), per mandare solo i cambi
	subs     map[int]chan Message
	nextSub  int
}

// Open legge game.json (assente = gioco nuovo, spento). Un file illeggibile si mette da parte
// (game.json.rotto-AAAAMMGG-HHMMSS) e si riparte da zero: meglio che non salvare più niente.
func Open(path string, lib *accessory.Library, lg *log.Logger, now func() time.Time) *Service {
	if now == nil {
		now = time.Now
	}
	if lg == nil {
		lg = log.New(discard{}, "", 0)
	}
	if lib == nil {
		lib = accessory.NewLibrary("")
	}
	s := &Service{Path: path, Lib: lib, Log: lg, Now: now, history: map[string]int{}, subs: map[int]chan Message{}}
	var st game.State
	if path != "" {
		d, err := readFile(path)
		switch {
		case errors.Is(err, fs.ErrNotExist):
		case err != nil:
			bad := path + ".rotto-" + now().Format("20060102-150405")
			if rerr := os.Rename(path, bad); rerr == nil {
				lg.Printf("gioco: %s illeggibile (%v): messo da parte in %s, si riparte da zero", path, err, bad)
			} else {
				lg.Printf("gioco: %s illeggibile (%v) e non spostabile (%v): solo in memoria", path, err, rerr)
				s.Path = ""
			}
		default:
			st, s.proposal = d.State, d.Proposal
			if d.History != nil {
				s.history = d.History
			}
		}
	}
	cat := lib.Catalog()
	s.libRev = lib.Rev()
	s.eng = game.New(st, cat.UnlockOrder(), now)
	return s
}

type discard struct{}

func (discard) Write(p []byte) (int, error) { return len(p), nil }

func readFile(path string) (fileData, error) {
	var d fileData
	b, err := os.ReadFile(path)
	if err != nil {
		return d, err
	}
	dec := json.NewDecoder(bytes.NewReader(b))
	if err := dec.Decode(&d); err != nil {
		return d, err
	}
	if d.Format != FileFormat || d.Version != 1 {
		return d, fmt.Errorf("formato %q versione %d, atteso %s 1", d.Format, d.Version, FileFormat)
	}
	return d, nil
}

// saveLocked scrive game.json in modo atomico. Con s.mu.
func (s *Service) saveLocked() error {
	s.dirty = false
	if s.Path == "" {
		return nil
	}
	d := fileData{Format: FileFormat, Version: 1, State: s.eng.State(), History: s.history, Proposal: s.proposal}
	b, err := json.MarshalIndent(d, "", " ")
	if err != nil {
		return err
	}
	if err := writeAtomic(s.Path, append(b, '\n')); err != nil {
		s.dirty = true
		s.Log.Printf("gioco: salvataggio di %s: %v", s.Path, err)
		return err
	}
	return nil
}

func writeAtomic(path string, data []byte) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, ".game-*.tmp")
	if err != nil {
		return err
	}
	_, werr := tmp.Write(data)
	if werr == nil {
		werr = tmp.Sync()
	}
	if cerr := tmp.Close(); werr == nil {
		werr = cerr
	}
	if werr == nil {
		werr = os.Chmod(tmp.Name(), 0o600)
	}
	if werr == nil {
		werr = os.Rename(tmp.Name(), path)
	}
	if werr != nil {
		_ = os.Remove(tmp.Name())
	}
	return werr
}

// Save: salvataggio subito (spegnimento del servizio, comandi da riga di comando a servizio spento).
func (s *Service) Save() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.saveLocked()
}

// refreshCatalogLocked: la libreria è cambiata (accessorio aggiunto o tolto): nuovo ordine di sblocco.
func (s *Service) refreshCatalogLocked() accessory.Catalog {
	cat := s.Lib.Catalog()
	if rev := s.Lib.Rev(); rev != s.libRev {
		s.libRev = rev
		s.eng.SetCatalog(cat.UnlockOrder())
	}
	return cat
}

// Apply: un evento delle sessioni (sessions.Engine.OnGame). Gli eventi con Key si salvano subito:
// dopo un riavvio la stessa chiave non dà XP una seconda volta.
func (s *Service) Apply(ev game.Event) {
	s.mu.Lock()
	cat := s.refreshCatalogLocked()
	gained, achs, levelUp := s.eng.Apply(ev)
	st := s.eng.State()
	if gained > 0 {
		s.history[st.Today] += gained
		s.pruneHistory(st.Today)
	}
	s.dirty = true
	if (ev.Key != "" && ev.Kind != game.KindActivity) || gained > 0 || len(achs) > 0 {
		_ = s.saveLocked()
	}
	var cel *Celebrate
	switch {
	case len(achs) > 0:
		for _, a := range game.Achievements() {
			if a.ID == achs[0] {
				cel = &Celebrate{ID: a.ID, Title: a.Title}
			}
		}
	case levelUp:
		cel = &Celebrate{ID: fmt.Sprintf("livello-%d", st.Level), Title: fmt.Sprintf("Livello %d", st.Level)}
	}
	if len(achs) > 0 || levelUp {
		s.Log.Printf("gioco: %+d XP (%s), livello %d, traguardi %v", gained, ev.Kind, st.Level, achs)
	}
	s.publishLocked(st, cat, cel)
	s.mu.Unlock()
}

func (s *Service) pruneHistory(today string) {
	t, err := time.Parse(time.DateOnly, today)
	if err != nil {
		return
	}
	cut := t.AddDate(0, 0, -HistoryDays).Format(time.DateOnly)
	for d := range s.history {
		if d < cut {
			delete(s.history, d)
		}
	}
}

// Tick: energia, stanchezza, mezzanotte (game.Engine.Tick), catalogo; salva se serve e manda
// `game` se è cambiato qualcosa. Ogni minuto (Run).
func (s *Service) Tick() {
	s.mu.Lock()
	cat := s.refreshCatalogLocked()
	s.eng.Tick(s.Now())
	if s.dirty {
		_ = s.saveLocked()
	}
	s.publishLocked(s.eng.State(), cat, nil)
	s.mu.Unlock()
}

// Run chiama Tick ogni minuto finché ctx non si chiude, poi salva.
func (s *Service) Run(done <-chan struct{}) {
	tk := time.NewTicker(time.Minute)
	defer tk.Stop()
	for {
		select {
		case <-done:
			_ = s.Save()
			return
		case <-tk.C:
			s.Tick()
		}
	}
}

// messageLocked costruisce il messaggio game. Spento: niente accessorio indossato né elenco degli
// sbloccati (la scheda non disegna accessori), ma livello ed XP sì (i progressi restano).
func (s *Service) messageLocked(st game.State, cat accessory.Catalog) Message {
	m := Message{T: "game", V: 1, On: st.On, Level: st.Level, XP: st.XP, XPLevelStart: game.LevelStart(st.Level),
		XPNext: st.XPNext, Energy: st.Energy, Tired: st.Tired && st.On, Unlocked: []Item{}}
	if !st.On {
		return m
	}
	m.Special = st.SpecialPending != ""
	if e := cat.Get(st.Wear); st.Wear != "" && e != nil {
		m.Wear = &Ref{ID: e.ID, CRC: e.CRC}
	}
	for _, id := range st.Unlocked {
		if e := cat.Get(id); e != nil {
			m.Unlocked = append(m.Unlocked, Item{ID: e.ID, Name: e.Name})
		}
	}
	if len(m.Unlocked) > MaxUnlocked {
		// i più recenti, con quello indossato sempre presente
		keep := m.Unlocked[len(m.Unlocked)-MaxUnlocked:]
		if m.Wear != nil && !slices.ContainsFunc(keep, func(i Item) bool { return i.ID == m.Wear.ID }) {
			keep = append([]Item{{ID: m.Wear.ID, Name: cat.Get(m.Wear.ID).Name}}, keep[1:]...)
		}
		m.Unlocked = keep
	}
	return m
}

// publishLocked manda il messaggio agli iscritti se è cambiato (o se c'è una festa). Con s.mu.
func (s *Service) publishLocked(st game.State, cat accessory.Catalog, cel *Celebrate) {
	m := s.messageLocked(st, cat)
	b, _ := json.Marshal(m)
	if cel == nil && bytes.Equal(b, s.last) {
		return
	}
	s.last = b
	if cel != nil && st.On {
		m.Celebrate = cel
	}
	for _, ch := range s.subs {
		select {
		case ch <- m:
		default: // iscritto lento: perde questo messaggio, il prossimo porta lo stato intero
		}
	}
}

// Message: il messaggio game di adesso (dopo hello e dopo l'autenticazione), senza festa.
func (s *Service) Message() Message {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.messageLocked(s.eng.State(), s.refreshCatalogLocked())
}

// Subscribe: un canale che riceve ogni messaggio game nuovo (con la festa, se c'è).
func (s *Service) Subscribe() (<-chan Message, func()) {
	ch := make(chan Message, 16)
	s.mu.Lock()
	id := s.nextSub
	s.nextSub++
	s.subs[id] = ch
	s.mu.Unlock()
	return ch, func() {
		s.mu.Lock()
		delete(s.subs, id)
		s.mu.Unlock()
	}
}

// On dice se il gioco è acceso.
func (s *Service) On() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.eng.State().On
}

// SetOn accende o spegne il gioco (webapp, CLI, menu della scheda). I progressi restano.
func (s *Service) SetOn(on bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.eng.State().On == on {
		return
	}
	s.eng.SetOn(on)
	s.Log.Printf("gioco: %s", map[bool]string{true: "acceso", false: "spento"}[on])
	_ = s.saveLocked()
	s.publishLocked(s.eng.State(), s.refreshCatalogLocked(), nil)
}

var (
	ErrUnknown   = errors.New("accessorio sconosciuto")
	ErrLocked    = errors.New("accessorio non ancora sbloccato")
	ErrNoSpecial = errors.New("nessun accessorio speciale da creare in questo momento")
)

// Wear: indossa un accessorio sbloccato e presente nella libreria ("" = nessuno).
func (s *Service) Wear(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	cat := s.refreshCatalogLocked()
	if id != "" && cat.Get(id) == nil {
		return fmt.Errorf("%w: %s", ErrUnknown, id)
	}
	if err := s.eng.Wear(id); err != nil {
		return fmt.Errorf("%w: %s", ErrLocked, id)
	}
	_ = s.saveLocked()
	s.publishLocked(s.eng.State(), cat, nil)
	return nil
}

// AddSpecial: un accessorio speciale appena aggiunto alla libreria (calamaio accessory add --special):
// si sblocca, chiude la proposta, si indossa e fa festa.
func (s *Service) AddSpecial(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Lib.Invalidate()
	cat := s.refreshCatalogLocked()
	e := cat.Get(id)
	if e == nil {
		return fmt.Errorf("%w: %s", ErrUnknown, id)
	}
	if err := s.eng.AddSpecial(id); err != nil {
		return err
	}
	_ = s.eng.Wear(id)
	s.proposal = Proposal{}
	_ = s.saveLocked()
	s.publishLocked(s.eng.State(), cat, &Celebrate{ID: "speciale-" + id, Title: e.Name})
	return nil
}

// LibraryChanged: un accessorio aggiunto da fuori (API o file): catalogo e messaggio.
func (s *Service) LibraryChanged() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Lib.Invalidate()
	s.publishLocked(s.eng.State(), s.refreshCatalogLocked(), nil)
}

// SpecialAnswer registra la risposta dell'utente alla proposta (Claude chiama
// `calamaio game special later|decline`).
func (s *Service) SpecialAnswer(answer string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	pending := s.eng.State().SpecialPending
	if pending == "" {
		return ErrNoSpecial
	}
	switch answer {
	case AnswerLater:
		if s.proposal.For != pending {
			s.proposal = Proposal{For: pending}
		}
		s.proposal.Answer = AnswerLater
		if s.proposal.Asked == "" {
			s.proposal.Asked = s.Now().Format(time.DateOnly)
		}
	case AnswerDecline:
		s.eng.DeclineSpecial()
		s.proposal = Proposal{}
	default:
		return fmt.Errorf("risposta %q: later o decline", answer)
	}
	s.Log.Printf("gioco: accessorio speciale per %s: %s", pending, answer)
	return s.saveLocked()
}

// ProposalContext: il testo per Claude (hook UserPromptSubmit sincrono) se c'è un accessorio
// speciale da proporre adesso: gioco acceso, proposta aperta e non già fatta oggi per lo stesso
// traguardo. Restituisce "" altrimenti. La proposta conta come fatta appena il testo esce.
func (s *Service) ProposalContext() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	st := s.eng.State()
	if !st.On || st.SpecialPending == "" {
		return ""
	}
	today := s.Now().Format(time.DateOnly)
	if s.proposal.For != st.SpecialPending {
		s.proposal = Proposal{For: st.SpecialPending}
	}
	if s.proposal.Asked == today {
		return ""
	}
	s.proposal.Asked, s.proposal.Answer = today, ""
	_ = s.saveLocked()
	title := st.SpecialPending
	for _, a := range game.Achievements() {
		if a.ID == st.SpecialPending {
			title = a.Title
		}
	}
	return fmt.Sprintf(ProposalText, title)
}

// ProposalText: il contesto per Claude (%s = titolo del traguardo).
const ProposalText = "Calamaio, gioco della mascotte: l'utente ha raggiunto il traguardo «%s» e il catalogo degli " +
	"accessori è esaurito, quindi ha diritto a un accessorio nuovo e speciale per la mascotte del display. " +
	"Quando è un buon momento (subito, se il prompt non è urgente; altrimenti alla fine di questa risposta) " +
	"chiediglielo UNA volta con AskUserQuestion: domanda «Creo un accessorio nuovo e speciale per la mascotte?», " +
	"opzioni «Sì», «Più tardi», «No». Sì: usa la skill calamaio:accessorio. Più tardi: esegui " +
	"`calamaio game special later` (te lo richiederò un altro giorno). No: esegui `calamaio game special decline`. " +
	"Se salta la domanda non insistere. Poi torna al lavoro che l'utente ha chiesto."

// Status: tutto il gioco per la webapp e per `calamaio game status`.
type Status struct {
	On             bool                `json:"on"`
	Level          int                 `json:"level"`
	XP             int                 `json:"xp"`
	XPLevelStart   int                 `json:"xpLevelStart"`
	XPNext         int                 `json:"xpNext"`
	Energy         int                 `json:"energy"`
	Tired          bool                `json:"tired"`
	Streak         int                 `json:"streak"`
	TodayXP        int                 `json:"todayXP"`
	DailyCap       int                 `json:"dailyCap"`
	Wear           string              `json:"wear"`
	Unlocked       []string            `json:"unlocked"`
	SpecialPending *Achievement        `json:"specialPending"` // traguardo che dà diritto all'accessorio speciale
	Proposal       Proposal            `json:"proposal"`
	History        []DayXP             `json:"history"` // ultimi 30 giorni, dal più vecchio
	Achievements   []Achievement       `json:"achievements"`
	Accessories    []AccessoryView     `json:"accessories"`
	Problems       []accessory.Problem `json:"problems"`
}

// DayXP: XP guadagnati in un giorno.
type DayXP struct {
	Day string `json:"day"`
	XP  int    `json:"xp"`
}

// Achievement: un traguardo, raggiunto (At) o no.
type Achievement struct {
	ID          string     `json:"id"`
	Title       string     `json:"title"`
	Description string     `json:"description"`
	At          *time.Time `json:"at"`
}

// AccessoryView: un accessorio della libreria per la webapp.
type AccessoryView struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	Author      string `json:"author"`
	Description string `json:"description"`
	Special     bool   `json:"special"`
	Source      string `json:"source"`
	CRC         uint32 `json:"crc"`
	Bytes       int    `json:"bytes"`
	Unlocked    bool   `json:"unlocked"`
	Worn        bool   `json:"worn"`
}

// Status: il gioco adesso. days = giorni di storico (0 = 30).
func (s *Service) Status(days int) Status {
	if days <= 0 {
		days = 30
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	cat := s.refreshCatalogLocked()
	st := s.eng.State()
	out := Status{On: st.On, Level: st.Level, XP: st.XP, XPLevelStart: game.LevelStart(st.Level), XPNext: st.XPNext,
		Energy: st.Energy, Tired: st.Tired && st.On, Streak: st.Streak, DailyCap: game.DailyCap, Wear: st.Wear,
		Unlocked: append([]string{}, st.Unlocked...), Proposal: s.proposal, Problems: cat.Problems}
	today := s.Now().Format(time.DateOnly)
	if st.Today == today {
		out.TodayXP = st.TodayXP
	}
	t, _ := time.Parse(time.DateOnly, today)
	for i := days - 1; i >= 0; i-- {
		d := t.AddDate(0, 0, -i).Format(time.DateOnly)
		out.History = append(out.History, DayXP{Day: d, XP: s.history[d]})
	}
	for _, a := range game.Achievements() {
		v := Achievement{ID: a.ID, Title: a.Title, Description: a.Description}
		if at, ok := st.Achievements[a.ID]; ok {
			at := at
			v.At = &at
		}
		out.Achievements = append(out.Achievements, v)
		if a.ID == st.SpecialPending {
			p := v
			out.SpecialPending = &p
		}
	}
	for _, e := range cat.Entries {
		out.Accessories = append(out.Accessories, AccessoryView{ID: e.ID, Name: e.Name, Author: e.Author,
			Description: e.Description, Special: e.Special, Source: e.Source, CRC: e.CRC, Bytes: len(e.Bin),
			Unlocked: slices.Contains(st.Unlocked, e.ID), Worn: st.Wear == e.ID})
	}
	if out.Problems == nil {
		out.Problems = []accessory.Problem{}
	}
	return out
}

// HistoryMap: copia dello storico giorno → XP (test e diagnosi).
func (s *Service) HistoryMap() map[string]int {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make(map[string]int, len(s.history))
	for k, v := range s.history {
		out[k] = v
	}
	return out
}

// State: copia dello stato del motore (test e diagnosi).
func (s *Service) State() game.State {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.eng.State()
}

// AccessoryPacket: il binario CACC di id (risposta ad accessory-need); false se sconosciuto.
func (s *Service) AccessoryPacket(id string) (uint32, []byte, bool) {
	e := s.Lib.Get(id)
	if e == nil {
		return 0, nil, false
	}
	return e.CRC, e.Bin, true
}
