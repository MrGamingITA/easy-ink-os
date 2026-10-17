// calamaio: servizio sul PC che collega le sessioni di Claude Code al display e-ink.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"calamaio/internal/autostart"
	"calamaio/internal/backup"
	"calamaio/internal/capture"
	"calamaio/internal/client"
	"calamaio/internal/config"
	"calamaio/internal/display"
	"calamaio/internal/fake"
	"calamaio/internal/ota"
	"calamaio/internal/server"
	"calamaio/internal/setup"
)

var version = "0.9.0-game"

const usage = `calamaio ` + "%s" + `

Uso:
  calamaio serve [--fake] [--fake-interval 4s] [--no-mdns] [--capture] [--samples DIR] [--firmware DIR]
                 [--factory-backup FILE [--factory-mac MAC] [--factory-chip C] [--factory-name N]]
                                    servizio: webapp e API su http://127.0.0.1:8765, display su ws://:8766/ws;
                                    sessioni vere da hook e status line, oppure finte con --fake
  calamaio setup claude [--undo]    imposta (o toglie) la status line di Calamaio in ~/.claude/settings.json
  calamaio status                   avvio automatico, display collegati (con abbinamento), sessioni, ultimi eventi
  calamaio install [--yes] [-- opzioni di serve]
                                    avvio automatico al login (macOS: LaunchAgent; Linux: systemd utente)
  calamaio install --status         dice se l'avvio automatico è installato
  calamaio uninstall [--yes]        toglie l'avvio automatico
  calamaio pair [--board B]         abbina un display: scrivi il codice a 6 cifre che mostra
  calamaio ota keygen [--force]     crea la chiave di firma degli aggiornamenti (<config>/calamaio/ota-key)
  calamaio ota push [--board B] [--yes] [--version V] [--device D] [FILE | --build DIR]
                                    aggiorna una scheda senza cavo (senza FILE: il pacchetto firmware del servizio)
  calamaio logs [--board B] [--follow] [--minutes N]
                                    registro della scheda (salvato in <dati>/calamaio/logs/<scheda>-<data>.log)
  calamaio unpair B                 dimentica l'abbinamento del display B
  calamaio wifi list [--board B]    reti Wi-Fi salvate sulla scheda (in uso, segnale)
  calamaio wifi add [--board B] [--stdin] SSID
                                    aggiunge una rete: la password si chiede nascosta (con --stdin: prima
                                    riga dello standard input); va alla scheda cifrata
  calamaio wifi forget [--board B] [--yes] SSID
                                    la scheda dimentica la rete e si riavvia (chiede conferma)
  calamaio workday start|stop|status [--board B]
                                    giornata di lavoro: conto alla rovescia fino all'orario di fine, tenuto
                                    dalla scheda (funziona anche senza PC)
  calamaio workday hours INIZIO FINE [--board B]
                                    orari della giornata (es. 09:00 18:00; la fine è sempre l'orario di fine)
  calamaio fake <sessione> <stato> [dettaglio]
                                    forza uno stato finto (serve --fake); stato "auto" lo rilascia
  calamaio backup list | import FILE --mac MAC [--name N] | rm ID
                                    archivio dei backup della flash (l'originale di fabbrica è protetto)
  calamaio avatar list [--free] [--json] | add FILE [--replace] | assign ID [--project DIR | --session ID]
                 | unassign [--project DIR | --session ID] | default [ID] | show ID
                                    avatar delle sessioni sul display (docs/AVATAR.md)
  calamaio game status [--json] | on | off | special later|decline
                                    gioco della mascotte: livello, XP, traguardi; accende o spegne
  calamaio accessory list [--json] | add FILE [--special] [--replace] | wear ID|none | show ID
                                    accessori della mascotte (docs/AVATAR.md §14)
  calamaio hook [--wait | --context]
                                    chiamato dagli hook di Claude Code
  calamaio statusline               usato come status line di Claude Code
  calamaio version
`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintf(os.Stderr, usage, version)
		os.Exit(2)
	}
	cmd, args := os.Args[1], os.Args[2:]
	switch cmd {
	case "serve":
		os.Exit(serve(args))
	case "hook":
		// Mai un codice d'uscita diverso da 0 e mai output su errore.
		fs := flag.NewFlagSet("hook", flag.ContinueOnError)
		fs.SetOutput(discard{})
		wait := fs.Bool("wait", false, "")
		ctxMode := fs.Bool("context", false, "")
		_ = fs.Parse(args)
		c, _ := config.Load()
		if *ctxMode {
			// SessionStart e UserPromptSubmit sincroni: contesto per Claude se il progetto non ha un
			// avatar, o se c'è da proporre l'accessorio speciale del gioco.
			client.HookContext(c, os.Stdin, os.Stdout, os.Getenv)
			return
		}
		client.Hook(c, os.Stdin, os.Stdout, *wait)
	case "statusline":
		c, _ := config.Load()
		client.StatusLine(c, os.Stdin, os.Stdout)
	case "status":
		if env, err := autostart.Detect(); err == nil {
			printAutostart(os.Stdout, env.Stat(autostart.ExecRunner{}))
		}
		c, _ := config.Load()
		if err := client.Status(c, os.Stdout, time.Now()); err != nil {
			fmt.Fprintln(os.Stderr, "calamaio:", err)
			os.Exit(1)
		}
	case "install":
		os.Exit(installCmd(args, realInstallDeps()))
	case "uninstall":
		os.Exit(uninstallCmd(args, realInstallDeps()))
	case "pair":
		os.Exit(pairCmd(args))
	case "unpair":
		os.Exit(unpairCmd(args))
	case "ota":
		os.Exit(otaCmd(args))
	case "logs":
		os.Exit(logsCmd(args))
	case "wifi":
		os.Exit(wifiCmd(args))
	case "workday":
		os.Exit(workdayCmd(args))
	case "fake":
		os.Exit(fakeCmd(args))
	case "setup":
		os.Exit(setupCmd(args))
	case "backup":
		os.Exit(backupCmd(args, os.Stdin, os.Stdout, os.Stderr))
	case "avatar":
		c, _ := config.Load()
		os.Exit(client.Avatar(c, args, client.AvatarIO{Out: os.Stdout, Err: os.Stderr}))
	case "game":
		c, _ := config.Load()
		os.Exit(client.Game(c, args, client.GameIO{Out: os.Stdout, Err: os.Stderr}))
	case "accessory":
		c, _ := config.Load()
		os.Exit(client.Accessory(c, args, client.GameIO{Out: os.Stdout, Err: os.Stderr}))
	case "version", "--version", "-v":
		fmt.Println(version)
	default:
		fmt.Fprintf(os.Stderr, usage, version)
		os.Exit(2)
	}
}

func serve(args []string) int {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	capt := fs.Bool("capture", false, "salva ogni payload ricevuto in file JSONL")
	samples := fs.String("samples", "protocol/samples", "cartella dei file di cattura")
	fk := fs.Bool("fake", false, "manda ai display lo stato finto (prove) invece delle sessioni vere")
	fkEvery := fs.Duration("fake-interval", 4*time.Second, "ogni quanto evolve lo stato finto (0 = fermo)")
	noMDNS := fs.Bool("no-mdns", false, "non annunciare il servizio con mDNS")
	loopback := fs.Bool("loopback", false, "WebSocket dei display e porta dei telefoni solo su 127.0.0.1 (prove dell'app nel simulatore)")
	fwDir := fs.String("firmware", "", "cartella del pacchetto firmware per la webapp (es. firmware/.pio/build/heltec213)")
	var fb backup.FactorySource
	fs.StringVar(&fb.File, "factory-backup", "", "flash di fabbrica da importare (protetta) se nell'archivio manca")
	fs.StringVar(&fb.MAC, "factory-mac", "", "MAC della scheda di --factory-backup (vuoto = dal nome del file, …_aabbccdd48e8_…)")
	fs.StringVar(&fb.Chip, "factory-chip", "", "chip di --factory-backup (vuoto = dall'intestazione del bootloader)")
	fs.StringVar(&fb.Board, "factory-board", "", "scheda di --factory-backup (vuoto = calamaio-xxxx dal MAC)")
	fs.StringVar(&fb.Name, "factory-name", "", "nome del backup di --factory-backup")
	fs.StringVar(&fb.SHA256, "factory-sha256", "", "SHA-256 atteso di --factory-backup (vuoto = SHA256.txt accanto)")
	_ = fs.Parse(args)

	lg := log.New(os.Stderr, "calamaio ", log.LstdFlags)
	c, err := config.Load()
	if err != nil {
		lg.Printf("config: %v (uso i valori di default)", err)
	}

	var cw *capture.Writer
	if *capt {
		cw, err = capture.New(*samples)
		if err != nil {
			lg.Printf("cattura: %v", err)
			return 1
		}
		lg.Printf("cattura attiva in %s", cw.Dir())
	}

	keys, err := config.NewKeyStore()
	if err != nil {
		lg.Printf("abbinamenti: %v (le chiavi restano in memoria)", err)
	}
	var ks display.KeyStore
	if keys != nil {
		ks = keys
	}
	avDir, err := config.AvatarDir()
	if err != nil {
		lg.Printf("avatar: cartella dei dati: %v (solo gli avatar predefiniti)", err)
	}
	avStore, err := config.NewAvatarStore()
	if err != nil {
		lg.Printf("avatar: %v (assegnazioni solo in memoria)", err)
		avStore = &config.AvatarStore{}
	}

	// SIGTERM: launchd al logout e allo spegnimento, systemd, kill; SIGINT: Ctrl-C; SIGHUP: terminale
	// chiuso. In tutti i casi spegnimento pulito con `bye` ai display (M6).
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	defer stop()
	var autostartFn func() any
	if env, err := autostart.Detect(); err == nil {
		autostartFn = func() any { return env.Stat(nil) }
	}
	o := server.Options{
		Config: c, Capture: cw, Fake: *fk, FakeInterval: *fkEvery,
		MDNS: !*noMDNS, Loopback: *loopback, Version: version, Log: lg, FirmwareDir: absPath(*fwDir),
		FactoryBackups: factorySources(c, fb), Keys: ks, AvatarDir: avDir, AvatarStore: avStore,
		Autostart: autostartFn,
	}
	// Aggiornamento remoto e diagnosi (docs/OTA.md).
	if p, err := ota.DefaultKeyPath(); err == nil {
		o.OTAKeyPath = p
	}
	if d, err := config.DataDir(); err == nil {
		dataPaths(&o, d)
	}
	err = server.Run(ctx, o)
	if err != nil {
		lg.Printf("%v", err)
		return 1
	}
	return 0
}

// dataPaths: i file del servizio nella cartella dei dati d.
func dataPaths(o *server.Options, d string) {
	o.CrashDir, o.DeviceLogDir, o.ElfDir = filepath.Join(d, "crash"), filepath.Join(d, "logs"), filepath.Join(d, "elf")
	o.GameFile, o.AccessoryDir = filepath.Join(d, "game.json"), filepath.Join(d, "accessories")
	o.ThemeDir = filepath.Join(d, "themes")
	o.PhonesFile, o.AppTLSDir = filepath.Join(d, "phones.json"), filepath.Join(d, "app-tls")
}

// fakeCmd: calamaio fake <sessione> <stato> [dettaglio…]
func fakeCmd(args []string) int {
	fs := flag.NewFlagSet("fake", flag.ExitOnError)
	summary := fs.String("summary", "", "riepilogo (stato done)")
	errCode := fs.String("error", "", "motivo (stato error): rate_limit, overloaded, …")
	hold := fs.Int("hold", 60, "secondi in cui il copione non tocca la sessione (0 = finché non arriva \"auto\")")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "uso: calamaio fake [--summary S] [--error E] [--hold N] <sessione> <stato> [dettaglio]")
		fmt.Fprintln(os.Stderr, "stati: working permission input done error idle compact, oppure auto")
	}
	_ = fs.Parse(args)
	if fs.NArg() < 2 {
		fs.Usage()
		return 2
	}
	r := fake.ForceRequest{Session: fs.Arg(0), State: fs.Arg(1), Error: *errCode, HoldS: hold}
	if fs.NArg() > 2 {
		d := strings.Join(fs.Args()[2:], " ")
		r.Detail = &d
	}
	if *summary != "" {
		r.Summary = summary
	}
	c, _ := config.Load()
	if err := client.Fake(c, r, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// pairCmd: calamaio pair [--board B]
func pairCmd(args []string) int {
	fs := flag.NewFlagSet("pair", flag.ExitOnError)
	board := fs.String("board", "", "display da abbinare (es. calamaio-3f2a); vuoto = l'unico collegato, o si sceglie")
	_ = fs.Parse(args)
	c, _ := config.Load()
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	defer signal.Stop(sig)
	err := client.Pair(c, *board, os.Stdin, os.Stdout, sig)
	switch {
	case errors.Is(err, client.ErrPairFailed):
		return 1
	case err != nil:
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// unpairCmd: calamaio unpair B
func unpairCmd(args []string) int {
	if len(args) != 1 || strings.HasPrefix(args[0], "-") {
		fmt.Fprintln(os.Stderr, "uso: calamaio unpair <display>   (es. calamaio unpair calamaio-3f2a)")
		return 2
	}
	c, _ := config.Load()
	if err := client.Unpair(c, args[0], os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// setupCmd: calamaio setup claude [--undo]
func setupCmd(args []string) int {
	if len(args) == 0 || args[0] != "claude" {
		fmt.Fprintln(os.Stderr, "uso: calamaio setup claude [--undo]")
		return 2
	}
	fs := flag.NewFlagSet("setup claude", flag.ExitOnError)
	undo := fs.Bool("undo", false, "rimette la status line di prima")
	_ = fs.Parse(args[1:])
	o := setup.Options{In: os.Stdin, Out: os.Stdout}
	var err error
	if *undo {
		err = setup.Undo(o)
	} else {
		err = setup.Install(o)
	}
	switch {
	case errors.Is(err, setup.ErrAborted):
		return 1
	case err != nil:
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// absPath rende assoluto un percorso relativo alla cartella da cui si avvia il servizio.
func absPath(p string) string {
	if p == "" {
		return ""
	}
	if a, err := filepath.Abs(p); err == nil {
		return a
	}
	return p
}

type discard struct{}

func (discard) Write(p []byte) (int, error) { return len(p), nil }

// otaCmd: calamaio ota keygen [--force] | push [--board B] [--yes] [--version V] [--device D] [FILE | --build DIR]
func otaCmd(args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "uso: calamaio ota keygen [--force] | calamaio ota push [--board B] [--yes] [FILE | --build DIR]")
		return 2
	}
	switch args[0] {
	case "keygen":
		fs := flag.NewFlagSet("ota keygen", flag.ExitOnError)
		force := fs.Bool("force", false, "sostituisce la chiave esistente (le schede la accettano solo dopo un nuovo abbinamento)")
		_ = fs.Parse(args[1:])
		path, err := ota.DefaultKeyPath()
		if err == nil {
			err = client.OtaKeygen(path, *force, os.Stdout)
		}
		if err != nil {
			fmt.Fprintln(os.Stderr, "calamaio:", err)
			return 1
		}
		return 0
	case "push":
		fs := flag.NewFlagSet("ota push", flag.ExitOnError)
		var o client.OtaPushOptions
		fs.StringVar(&o.Board, "board", "", "scheda da aggiornare (es. calamaio-3f2a); vuoto = l'unica collegata, o si sceglie")
		build := fs.String("build", "", "cartella della build (es. firmware/.pio/build/heltec213)")
		fs.StringVar(&o.Version, "version", "", "versione dell'immagine, se non si ricava da sola (deve comparire nell'immagine)")
		fs.StringVar(&o.Device, "device", "", "profilo della scheda dell'immagine (es. heltec213), se non si ricava da solo")
		fs.BoolVar(&o.Yes, "yes", false, "non chiedere conferma")
		_ = fs.Parse(reorder(args[1:], map[string]bool{"--yes": true, "-yes": true}))
		switch {
		case *build != "" && fs.NArg() > 0:
			fmt.Fprintln(os.Stderr, "calamaio: indica un file oppure --build DIR, non tutti e due")
			return 2
		case *build != "":
			o.Path = *build
		case fs.NArg() == 1:
			o.Path = fs.Arg(0)
		case fs.NArg() > 1:
			fmt.Fprintln(os.Stderr, "calamaio: un solo file")
			return 2
		}
		if o.Path != "" {
			o.Path = absPath(o.Path)
		}
		c, _ := config.Load()
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
		defer signal.Stop(sig)
		err := client.OtaPush(c, o, os.Stdin, os.Stdout, sig)
		switch {
		case errors.Is(err, client.ErrOtaFailed):
			return 1
		case err != nil:
			fmt.Fprintln(os.Stderr, "calamaio:", err)
			return 1
		}
		return 0
	}
	fmt.Fprintln(os.Stderr, "uso: calamaio ota keygen [--force] | calamaio ota push [--board B] [--yes] [FILE | --build DIR]")
	return 2
}

// logsCmd: calamaio logs [--board B] [--follow] [--minutes N]
func logsCmd(args []string) int {
	fs := flag.NewFlagSet("logs", flag.ExitOnError)
	var o client.LogsOptions
	fs.StringVar(&o.Board, "board", "", "scheda (es. calamaio-3f2a); vuoto = l'unica collegata, o si sceglie")
	fs.BoolVar(&o.Follow, "follow", false, "dopo il contenuto dell'anello, le righe nuove in diretta")
	fs.IntVar(&o.Minutes, "minutes", 10, "con --follow: per quanti minuti (al massimo 60)")
	_ = fs.Parse(args)
	if o.Minutes < 1 || o.Minutes > 60 {
		fmt.Fprintln(os.Stderr, "calamaio: --minutes da 1 a 60")
		return 2
	}
	c, _ := config.Load()
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	defer signal.Stop(sig)
	if err := client.Logs(c, o, os.Stdin, os.Stdout, sig); err != nil {
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// reorder sposta i flag dopo gli argomenti posizionali davanti (flag si ferma al primo argomento:
// «calamaio ota push firmware.bin --yes» deve funzionare). bools: flag senza valore.
func reorder(args []string, bools map[string]bool) []string {
	var flags, pos []string
	for i := 0; i < len(args); i++ {
		a := args[i]
		switch {
		case a == "--":
			pos = append(pos, args[i+1:]...)
			i = len(args)
		case strings.HasPrefix(a, "-") && len(a) > 1:
			flags = append(flags, a)
			if !bools[a] && !strings.Contains(a, "=") && i+1 < len(args) {
				flags = append(flags, args[i+1])
				i++
			}
		default:
			pos = append(pos, a)
		}
	}
	return append(flags, pos...)
}
