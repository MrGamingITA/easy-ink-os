// Package server mette insieme i pezzi di `calamaio serve`: API locale, WebSocket dei display,
// annuncio mDNS e (in M3) generatore dello stato finto.
package server

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/api"
	"calamaio/internal/avatar"
	"calamaio/internal/backup"
	"calamaio/internal/capture"
	"calamaio/internal/config"
	"calamaio/internal/diag"
	"calamaio/internal/display"
	"calamaio/internal/fake"
	"calamaio/internal/firmware"
	"calamaio/internal/game"
	"calamaio/internal/gamesvc"
	"calamaio/internal/mdns"
	"calamaio/internal/ota"
	"calamaio/internal/phones"
	"calamaio/internal/sessions"
	"calamaio/internal/state"
	"calamaio/internal/theme"
	"calamaio/internal/tz"
	"calamaio/internal/webui"
)

type Options struct {
	Config       config.Config
	Capture      *capture.Writer // nil = cattura spenta
	Fake         bool
	FakeInterval time.Duration // 0 = stato finto fermo (si cambia solo con /debug/fake)
	MDNS         bool
	// Loopback: WebSocket dei display e porta dei telefoni solo su 127.0.0.1, QR con h=127.0.0.1
	// (prove dell'app nel simulatore, che vede il loopback del Mac). La 8765 è comunque solo locale.
	Loopback bool
	// AnnounceApp (prove): al posto del responder vero, annuncia i telefoni con questa funzione.
	AnnounceApp func(port int, fp string) (stop func())
	Version     string
	Log         *log.Logger
	// FirmwareDir: pacchetto firmware per la webapp (vuoto = config, poi <dati utente>/calamaio/firmware).
	FirmwareDir string
	// TodayFile: dove si salvano le statistiche di Oggi (vuoto = <dati utente>/calamaio/oggi.json).
	TodayFile string
	// BackupDir: archivio dei backup della flash (vuoto = <dati utente>/calamaio/backups).
	BackupDir string
	// FactoryBackups: originali di fabbrica da importare all'avvio se mancano (protetti).
	FactoryBackups []backup.FactorySource
	// Keys: chiavi di abbinamento dei display (M5). nil = solo in memoria, si perdono al riavvio
	// (test); `calamaio serve` usa config.json (config.KeyStore).
	Keys display.KeyStore
	// AvatarDir: libreria degli avatar dell'utente (vuota = solo i predefiniti, test); `calamaio serve`
	// usa <dati utente>/calamaio/avatars.
	AvatarDir string
	// AvatarStore: assegnazioni degli avatar. nil = solo in memoria (test); `calamaio serve` usa
	// config.json (config.AvatarStore).
	AvatarStore avatar.Assigner
	// AvatarPoll: ogni quanto controllare libreria e config.json per le modifiche fatte da fuori
	// (0 = 5 s).
	AvatarPoll time.Duration
	// Gioco della mascotte: stato (vuoto = solo in memoria, test; `calamaio serve` usa
	// <dati utente>/calamaio/game.json) e libreria degli accessori dell'utente (vuota = solo i
	// predefiniti; `calamaio serve` usa <dati utente>/calamaio/accessories).
	GameFile     string
	ThemeDir     string // temi delle schermate (vuoto = nessun archivio; calamaio serve: <dati utente>/calamaio/themes)
	AccessoryDir string
	// GameClock (facoltativo, test): orologio del gioco al posto di time.Now. Le regole dipendono
	// dall'ora (dalle 00 alle 06 il lavoro non conta), quindi i test non devono usare l'ora vera.
	GameClock func() time.Time

	// Facoltativi, per i test: listener già aperti al posto delle porte della config.
	APIListener net.Listener
	WSListener  net.Listener
	// Telefoni (app iPhone): elenco (vuoto = solo in memoria, test; calamaio serve: <dati>/phones.json) e
	// cartella del certificato (vuota = certificato nuovo in memoria a ogni avvio, test; <dati>/app-tls).
	PhonesFile string
	AppTLSDir  string
	// AppListen apre la porta dei telefoni (nil = :AppPort); AppHosts dà gli indirizzi del QR (nil =
	// phones.LocalIPv4s). Nei test: 127.0.0.1.
	AppListen func() (net.Listener, error)
	AppHosts  func() []string
	// TuneApp, se non nil, configura i telefoni prima di partire (test: finestra più corta).
	TuneApp func(*phones.Manager)
	// AppRetry, se non nil, dà l'attesa prima del tentativo n (0, 1, …) di riaprire la porta dei telefoni
	// occupata (test: millisecondi). nil = appRetryDelay.
	AppRetry func(n int) time.Duration
	// Hub, se non nil, viene configurato prima di partire (test: intervalli più corti).
	TuneHub func(*display.Hub)
	// TuneAPI, se non nil, configura la API prima di partire (test).
	TuneAPI func(*api.Server)
	// Ready viene chiamata quando tutto ascolta.
	Ready func(r Running)
	// ByeWait: quanto aspettare l'invio di `bye` allo spegnimento pulito (0 = DefaultByeWait).
	ByeWait time.Duration
	// Autostart, se non nil, dice se il servizio parte da solo al login (GET /api/status).
	Autostart func() any

	// Aggiornamento remoto e diagnosi (docs/OTA.md). Vuoti = spenti (test); `calamaio serve` usa
	// <config>/calamaio/ota-key e <dati utente>/calamaio/{crash,logs,elf}.
	OTAKeyPath   string // chiave di firma Ed25519
	CrashDir     string // rapporti di crash
	DeviceLogDir string // registro da remoto delle schede
	ElfDir       string // archivio degli ELF delle build mandate (per tradurre i crash)
	// Addr2line: percorso di xtensa-esp32s3-elf-addr2line ("" = cercato in PlatformIO e nel PATH).
	Addr2line string
}

// DefaultByeWait: allo spegnimento pulito si aspetta al massimo questo l'invio di `bye` (M6).
const DefaultByeWait = 2 * time.Second

// Running espone i componenti avviati (test e diagnostica).
type Running struct {
	APIAddr string
	WSAddr  string
	Store   *state.Store
	Hub     *display.Hub
	Fake    *fake.Generator
	Engine  *sessions.Engine // nil con --fake
	Avatars *avatar.Manager
	Game    *gamesvc.Service
	AppAddr func() string // porta dei telefoni ("" = spenta)
	Phones  *phones.Manager
}

// Run blocca finché ctx non viene chiuso, poi spegne tutto in ordine.
func Run(ctx context.Context, o Options) error {
	lg := o.Log
	c := o.Config

	apiLn := o.APIListener
	if apiLn == nil {
		addr := fmt.Sprintf("127.0.0.1:%d", c.APIPort)
		ln, err := net.Listen("tcp", addr)
		if err != nil {
			return fmt.Errorf("impossibile ascoltare su %s: %w", addr, err)
		}
		apiLn = ln
	}
	defer apiLn.Close()
	wsLn := o.WSListener
	if wsLn == nil {
		// Tutte le interfacce: i display arrivano dalla rete locale (con Loopback solo 127.0.0.1).
		addr := fmt.Sprintf(":%d", c.WSPort)
		if o.Loopback {
			addr = fmt.Sprintf("127.0.0.1:%d", c.WSPort)
		}
		ln, err := net.Listen("tcp", addr)
		if err != nil {
			return fmt.Errorf("impossibile ascoltare su %s: %w", addr, err)
		}
		wsLn = ln
	}
	defer wsLn.Close()

	// Stato: finto (--fake, per le prove) oppure costruito dagli hook e dalla status line veri.
	var store *state.Store
	var gen *fake.Generator
	var eng *sessions.Engine
	if o.Fake {
		store = state.NewStore(fake.Seed(time.Now()))
		gen = fake.New(store)
	} else {
		store = state.NewStore(state.Empty())
		eng = sessions.New(store)
		eng.Log = lg
		if c.TestRegex != "" {
			re, err := regexp.Compile(c.TestRegex)
			if err != nil {
				lg.Printf("test_regex non valida (%v): uso quella predefinita", err)
			} else {
				eng.TestRe = re
			}
		}
		eng.TodayFile = o.TodayFile
		if eng.TodayFile == "" {
			if d, err := config.DataDir(); err == nil {
				eng.TodayFile = filepath.Join(d, "oggi.json")
			}
		}
		eng.LoadToday()
	}

	// Avatar: predefiniti inclusi nel binario + libreria dell'utente, assegnazioni in config.json.
	var as0 avatar.Assigner = o.AvatarStore
	if as0 == nil {
		as0 = &config.AvatarStore{}
	}
	avm := avatar.NewManager(avatar.NewLibrary(o.AvatarDir), as0)
	store.Avatars = avm
	if cat := avm.Lib.Catalog(); len(cat.Problems) > 0 {
		for _, p := range cat.Problems {
			lg.Printf("avatar: %s non caricato: %s", p.File, strings.Join(p.Errors, "; "))
		}
	}
	if d := avm.DefaultEntry(); d != nil {
		lg.Printf("avatar: %d nella libreria (utente: %s), predefinito %s", len(avm.Lib.Catalog().Entries), dirOrNone(o.AvatarDir), d.ID)
	}

	zone := c.TZ
	if zone == "" {
		z, src, err := tz.Local(time.Now())
		zone = z
		if err != nil {
			lg.Printf("fuso: %v; uso %q (sostituibile con \"tz\" nella config)", err, z)
		} else {
			lg.Printf("fuso POSIX %s (%s)", z, src)
		}
	} else {
		lg.Printf("fuso POSIX %s (dalla config)", zone)
	}

	// Gioco della mascotte: eventi dalle sessioni e dalle archiviazioni, messaggi game ai display.
	gsvc := gamesvc.Open(o.GameFile, accessory.NewLibrary(o.AccessoryDir), lg, o.GameClock)
	gameNow := time.Now
	if o.GameClock != nil {
		gameNow = o.GameClock
	}
	gst := gsvc.State()
	lg.Printf("gioco della mascotte: %s, livello %d, %d XP (%s)", map[bool]string{true: "acceso", false: "spento"}[gst.On], gst.Level, gst.XP, dirOrNone(o.GameFile))
	for _, p := range gsvc.Lib.Catalog().Problems {
		lg.Printf("accessori: %s non caricato: %s", p.File, strings.Join(p.Errors, "; "))
	}
	if eng != nil {
		eng.OnGame = func(ev game.Event) {
			if o.GameClock != nil {
				ev.At = gameNow() // test: l'ora del gioco, non quella dell'hook
			}
			gsvc.Apply(ev)
		}
	}
	store.OnArchive = func(ss state.Session) {
		if ss.State == state.Done { // archiviare una sessione finita la chiude (stessa chiave di SessionEnd)
			gsvc.Apply(game.Event{At: gameNow(), Session: ss.ID, Kind: game.KindSessionClosed, Key: sessions.ClosedKey(ss.ID)})
		}
	}

	hub := display.NewHub(store, lg)
	hub.Archiver = store
	hub.Avatars = avm
	hub.Game = gsvc
	hub.TZ = zone
	hub.Host = mdns.ShortHost()
	hub.Version = o.Version
	hub.Keys = o.Keys
	if err := hub.LoadKeys(); err != nil {
		lg.Printf("abbinamenti: %v (nessun display abbinato finché non si sistema config.json)", err)
	}
	if b := hub.PairedBoards(); len(b) > 0 {
		lg.Printf("display abbinati: %v", b)
	}
	fwDir := o.FirmwareDir
	if fwDir == "" {
		fwDir = c.FirmwarePath()
	}
	fwPkg := firmware.New(fwDir)
	if o.OTAKeyPath != "" {
		hub.OTAKey = &ota.KeyCache{Path: o.OTAKeyPath}
		if _, err := hub.OTAKey.Get(); err != nil {
			lg.Printf("aggiornamento remoto: %v", err)
		} else {
			lg.Printf("aggiornamento remoto: chiave di firma %s", o.OTAKeyPath)
		}
	}
	var logFiles *diag.LogFiles
	if o.DeviceLogDir != "" {
		logFiles = &diag.LogFiles{Dir: o.DeviceLogDir}
		hub.LogFiles = logFiles
	}
	var crashes *diag.CrashStore
	if o.CrashDir != "" {
		crashes = &diag.CrashStore{Dir: o.CrashDir, Addr2line: o.Addr2line, Elves: elfCandidates(fwPkg, o.ElfDir)}
		hub.Crashes = crashes
		if last := crashes.Last(""); last != nil {
			lg.Printf("ultimo crash: %s, firmware %s, %s (%s)", last.Board, last.FW, last.Reason,
				time.UnixMilli(last.Received).Local().Format("2006-01-02 15:04"))
		}
	}
	if o.TuneHub != nil {
		o.TuneHub(hub)
	}

	as := api.New(o.Capture, lg)
	as.Displays = hub
	as.State = store
	if gen != nil {
		as.Fake = gen
	}
	if eng != nil {
		as.Sink = eng
	}
	wsPort := wsLn.Addr().(*net.TCPAddr).Port
	as.Info = map[string]any{"version": o.Version, "ws_port": wsPort, "tz": zone, "mdns": o.MDNS}
	as.UI = webui.New(webui.Static())
	as.Remote = hub
	as.Source = store
	as.Firmware = fwPkg
	as.OTA = hub
	as.Wifi = hub
	as.Workday = hub
	as.ThemeSend = hub
	as.Crashes = crashes
	as.ElfDir = o.ElfDir
	as.LogFiles = logFiles
	as.Backups = openBackups(o, lg)
	as.Service = api.ServiceInfo{Version: o.Version, Host: hub.Host, TZ: zone, WSPort: wsPort}
	as.Pairing = hub
	as.Perms = hub
	as.Avatars = avm
	as.OnAvatarsChanged = store.Touch
	as.Game = gsvc
	if o.ThemeDir != "" {
		as.Themes = theme.NewStore(o.ThemeDir)
		lg.Printf("temi: %s", o.ThemeDir)
	}
	as.Autostart = o.Autostart
	if eng != nil {
		as.Tracker = eng
		eng.OnHookClosed = hub.ClosePermission // risposta nel terminale: Claude Code non chiude il hook
	}
	if o.TuneAPI != nil {
		o.TuneAPI(as)
	}

	// runCtx non discende da ctx: allo spegnimento le richieste di permesso in attesa si chiudono prima
	// con «service» (StopPermissions), poi si cancella tutto. Se discendesse da ctx, i hook in attesa
	// vedrebbero il loro contesto chiuso insieme e verrebbero presi per risposte nel terminale.
	runCtx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// BaseContext: allo spegnimento si chiudono anche le richieste lunghe (SSE di /api/events).
	base := func(net.Listener) context.Context { return runCtx }
	app := openApp(o, c, lg, as, base)
	as.Phones = app
	defer app.close()
	apiSrv := &http.Server{Handler: as.Handler(), ReadHeaderTimeout: 5 * time.Second, BaseContext: base}
	wsSrv := &http.Server{Handler: hub.Handler(), ReadHeaderTimeout: 5 * time.Second}
	var wg sync.WaitGroup
	errc := make(chan error, 3)
	serveOn := func(name string, s *http.Server, ln net.Listener) {
		defer wg.Done()
		if err := s.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errc <- fmt.Errorf("%s: %w", name, err)
		}
	}
	wg.Add(2)
	go serveOn("API", apiSrv, apiLn)
	go serveOn("WebSocket", wsSrv, wsLn)
	lg.Printf("webapp e API locale su http://%s (debug: /debug)", apiLn.Addr())
	lg.Printf("pacchetto firmware: %s", fwDir)
	lg.Printf("display: ws://%s/ws (tutte le interfacce)", wsLn.Addr())

	if gen != nil {
		if o.FakeInterval > 0 {
			wg.Add(1)
			go func() { defer wg.Done(); gen.Run(runCtx.Done(), o.FakeInterval) }()
			lg.Printf("stato finto: evolve ogni %s", o.FakeInterval)
		} else {
			lg.Printf("stato finto fermo: cambialo con `calamaio fake`")
		}
	} else {
		wg.Add(1)
		go func() { defer wg.Done(); eng.Run(runCtx) }()
		lg.Printf("sessioni vere: hook e status line di Claude Code (Oggi in %s)", eng.TodayFile)
	}
	wg.Add(1)
	go func() { defer wg.Done(); watchAvatars(runCtx, avm, store, o.AvatarPoll) }()
	wg.Add(2)
	go func() { defer wg.Done(); gsvc.Run(runCtx.Done()) }()
	go func() { defer wg.Done(); hub.RunGame(runCtx.Done()) }()
	if o.MDNS && o.Loopback {
		lg.Printf("mdns: spento con --loopback (le porte 8766 e 8767 sono solo su 127.0.0.1: nessun annuncio sulla rete)")
	} else if o.MDNS && o.AnnounceApp != nil {
		// prove: nessun responder vero, solo l'annuncio dei telefoni finto
		app.mu.Lock()
		app.announce = o.AnnounceApp
		app.mu.Unlock()
	} else if o.MDNS {
		rp, err := mdns.NewResponder(lg)
		if err != nil {
			lg.Printf("%v (i display dovranno usare l'indirizzo IP, i telefoni non saranno annunciati)", err)
		} else {
			rp.Announce(mdns.DisplayConfig(wsPort, "/ws"))
			app.mu.Lock()
			app.announce = func(port int, fp string) func() { return rp.Announce(mdns.AppConfig(port, fp)) }
			app.mu.Unlock()
			wg.Add(1)
			go func() {
				defer wg.Done()
				if err := rp.Run(runCtx); err != nil {
					lg.Printf("%v (i display dovranno usare l'indirizzo IP)", err)
				}
			}()
		}
	}

	app.sync() // con un telefono già abbinato la porta si accende all'avvio
	if o.Ready != nil {
		o.Ready(Running{APIAddr: apiLn.Addr().String(), WSAddr: wsLn.Addr().String(), Store: store, Hub: hub, Fake: gen, Engine: eng, Avatars: avm, Game: gsvc, AppAddr: app.addr, Phones: app.mgr})
	}

	var runErr error
	select {
	case <-ctx.Done():
	case runErr = <-errc:
	}
	hub.StopPermissions() // perm-close «service» ai display, i hook in attesa escono senza decisione
	if runErr == nil {
		// Spegnimento pulito (segnale): riepilogo della giornata a tutti i display, poi si chiude.
		// Dopo un errore niente bye: il display mostra «PC non raggiungibile» e riprova.
		wait := o.ByeWait
		if wait <= 0 {
			wait = DefaultByeWait
		}
		hub.Bye(byeMessage(store, eng, time.Now()), wait)
	}
	cancel()
	sctx, scancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer scancel()
	_ = wsSrv.Shutdown(sctx) // non tocca le connessioni già passate a WebSocket…
	hub.Close()              // …che chiude l'hub
	_ = apiSrv.Shutdown(sctx)
	wg.Wait()
	lg.Printf("fermato")
	return runErr
}

// byeMessage: il messaggio `bye` con il riepilogo della giornata (docs/PROTOCOLLO.md, «bye»).
func byeMessage(store *state.Store, eng *sessions.Engine, now time.Time) state.Bye {
	sum := store.ByeSummary(now)
	if eng != nil {
		day, n := eng.Day()
		sum.Day, sum.SessionsToday = day, &n
	} else {
		sum.Day = now.Format("2006-01-02")
	}
	return state.Bye{T: "bye", V: 1, Summary: sum.Fit(state.MaxByeBytes)}
}

// watchAvatars: le modifiche fatte da fuori (un file copiato a mano nella libreria, config.json
// cambiato da un altro processo) arrivano a display e webapp entro un intervallo. Quelle fatte con
// la API avvisano subito (OnAvatarsChanged).
func watchAvatars(ctx context.Context, m *avatar.Manager, store *state.Store, every time.Duration) {
	if every <= 0 {
		every = 5 * time.Second
	}
	last := m.Rev() + m.AssignmentsRev()
	tk := time.NewTicker(every)
	defer tk.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-tk.C:
			if now := m.Rev() + m.AssignmentsRev(); now != last {
				last = now
				store.Touch()
			}
		}
	}
}

func dirOrNone(d string) string {
	if d == "" {
		return "nessuna"
	}
	return d
}

// openBackups apre l'archivio dei backup e importa gli originali di fabbrica mancanti. Un problema
// qui non ferma il servizio: le rotte dei backup rispondono 404 e il motivo finisce nel log.
func openBackups(o Options, lg *log.Logger) *backup.Store {
	dir := o.BackupDir
	if dir == "" {
		d, err := config.DataDir()
		if err != nil {
			lg.Printf("backup: cartella dei dati: %v (archivio spento)", err)
			return nil
		}
		dir = filepath.Join(d, "backups")
	}
	bs, err := backup.Open(dir)
	if err != nil {
		lg.Printf("backup: %v (archivio spento)", err)
		return nil
	}
	lg.Printf("backup: archivio in %s", dir)
	for _, src := range o.FactoryBackups {
		res, err := bs.ImportFactory(src)
		switch {
		case err != nil:
			lg.Printf("backup: originale di fabbrica non importato: %v", err)
		case res.Imported:
			lg.Printf("backup: importato l'originale di fabbrica %s «%s» da %s (sha256 %s, protetto)",
				res.Backup.ID, res.Backup.Name, src.File, res.Backup.SHA256)
		default:
			lg.Printf("backup: originale di fabbrica di %s già presente (%s)", res.Backup.MAC, res.Backup.ID)
		}
	}
	return bs
}

// elfCandidates: gli ELF che possono tradurre gli indirizzi di un crash: quello del pacchetto firmware
// del servizio (con la sua versione) e quelli archiviati a ogni aggiornamento remoto.
func elfCandidates(pkg *firmware.Package, archive string) func() []diag.ElfCandidate {
	var (
		mu    sync.Mutex
		cache = map[string]elfSum{}
	)
	return func() []diag.ElfCandidate {
		var out []diag.ElfCandidate
		if pkg != nil && pkg.Dir != "" {
			p := filepath.Join(pkg.Dir, "firmware.elf")
			if st, err := os.Stat(p); err == nil {
				mu.Lock()
				e, ok := cache[p]
				if !ok || !e.mtime.Equal(st.ModTime()) || e.size != st.Size() {
					e = elfSum{mtime: st.ModTime(), size: st.Size(), sum: fileSum(p)}
					cache[p] = e
				}
				mu.Unlock()
				out = append(out, diag.ElfCandidate{Path: p, Version: pkg.Read().Version, SHA256: e.sum})
			}
		}
		if archive != "" {
			for _, a := range ota.ListELF(archive) {
				out = append(out, diag.ElfCandidate{Path: a.Path, Version: a.Version, SHA256: a.SHA256})
			}
		}
		return out
	}
}

type elfSum struct {
	mtime time.Time
	size  int64
	sum   string
}

func fileSum(path string) string {
	f, err := os.Open(path)
	if err != nil {
		return ""
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return ""
	}
	return hex.EncodeToString(h.Sum(nil))
}
