package server

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"strconv"
	"sync"
	"time"

	"calamaio/internal/api"
	"calamaio/internal/config"
	"calamaio/internal/mdns"
	"calamaio/internal/phones"
)

// appService: telefoni abbinati, finestra di abbinamento e porta HTTPS dei telefoni (spec app iPhone §3).
// La porta è accesa solo se c'è un telefono o una finestra aperta (phones.Manager.Wanted).
type appService struct {
	mgr      *phones.Manager
	cert     *tls.Certificate
	fp       string
	broken   error // elenco o certificato illeggibili: niente porta, niente abbinamento
	cfgPort  int
	listen   func() (net.Listener, error)
	hosts    func() []string
	name     string
	version  string
	handler  http.Handler
	base     func(net.Listener) context.Context
	lg       *log.Logger
	announce func(port int, fp string) (stop func()) // Bonjour (nil = spento), Task 5
	retryIn  func(n int) time.Duration               // attesa prima del tentativo n di riaprire la porta

	mu         sync.Mutex
	srv        *http.Server
	ln         net.Listener
	unannounce func()
	lastErr    error
	closed     bool        // dopo close() la porta non si riaccende
	retry      *time.Timer // porta occupata: prossimo tentativo (nil = nessuno)
	tries      int
}

// openApp prepara i telefoni; la porta si accende con sync().
func openApp(o Options, c config.Config, lg *log.Logger, as *api.Server, base func(net.Listener) context.Context) *appService {
	a := &appService{lg: lg, name: mdns.ShortHost(), version: o.Version, base: base, cfgPort: c.AppPort, retryIn: o.AppRetry}
	if a.retryIn == nil {
		a.retryIn = appRetryDelay
	}
	st, err := phones.Open(o.PhonesFile)
	if err != nil {
		lg.Printf("telefoni: %v: abbinamento spento, il file non si tocca", err)
		a.broken = fmt.Errorf("elenco dei telefoni illeggibile: %w", err)
		st, _ = phones.Open("")
	}
	a.mgr = phones.NewManager(st)
	if o.TuneApp != nil {
		o.TuneApp(a.mgr)
	}
	if a.broken == nil {
		cert, fp, err := phones.LoadOrCreateCert(o.AppTLSDir, a.name, time.Now())
		if err != nil {
			lg.Printf("telefoni: %v (porta spenta; per rifarlo cancella la cartella e riabbina i telefoni)", err)
			a.broken = err
		} else {
			a.cert, a.fp = &cert, fp
		}
	}
	a.listen = o.AppListen
	if a.listen == nil {
		host := ""
		if o.Loopback {
			host = "127.0.0.1"
		}
		addr := net.JoinHostPort(host, strconv.Itoa(c.AppPort))
		a.listen = func() (net.Listener, error) { return net.Listen("tcp", addr) }
	}
	a.hosts = o.AppHosts
	if a.hosts == nil {
		a.hosts = phones.LocalIPv4s
		if o.Loopback {
			a.hosts = func() []string { return []string{"127.0.0.1"} }
		}
	}
	a.handler = api.NewPhoneHandler(api.PhoneOptions{Auth: a.mgr, Whoami: a.whoami, Next: as.Handler(), Log: lg})
	a.mgr.OnChange = a.sync
	if n := st.Len(); n > 0 {
		lg.Printf("telefoni: %d abbinati", n)
	}
	return a
}

// sync accende o spegne la porta. Spegnere: il listener si chiude subito (la porta torna libera e nessuno si
// collega più), le richieste in corso hanno 1 s per finire (la risposta che ha chiuso la finestra arriva).
func (a *appService) sync() {
	a.mu.Lock()
	defer a.mu.Unlock()
	want := !a.closed && a.broken == nil && a.mgr.Wanted()
	if !want || a.srv != nil {
		a.stopRetry()
	}
	switch {
	case want && a.srv == nil:
		ln, err := a.listen()
		if err != nil {
			a.lastErr = fmt.Errorf("porta dei telefoni: %w", err)
			d := a.retryIn(a.tries)
			a.lg.Printf("telefoni: %v (riprovo fra %s)", a.lastErr, d)
			a.scheduleRetry(d)
			return
		}
		a.stopRetry()
		a.lastErr = nil
		tl := tls.NewListener(ln, &tls.Config{Certificates: []tls.Certificate{*a.cert}, MinVersion: tls.VersionTLS13, NextProtos: []string{"http/1.1"}})
		srv := &http.Server{Handler: a.handler, ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 2 * time.Minute,
			BaseContext: a.base, ErrorLog: log.New(io.Discard, "", 0)} // i TLS interrotti (sonde Bonjour) non vanno nel registro
		a.srv, a.ln = srv, ln
		go func() { _ = srv.Serve(tl) }()
		if a.announce != nil {
			a.unannounce = a.announce(tcpPort(ln), a.fp)
		}
		a.lg.Printf("telefoni: HTTPS su %s (impronta %s…)", ln.Addr(), a.fp[:16])
	case !want && a.srv != nil:
		srv := a.srv
		_ = a.ln.Close()
		a.srv, a.ln = nil, nil
		if a.unannounce != nil {
			a.unannounce()
			a.unannounce = nil
		}
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			if srv.Shutdown(ctx) != nil {
				_ = srv.Close()
			}
		}()
		a.lg.Printf("telefoni: porta spenta (nessun telefono e nessuna finestra aperta)")
	}
}

// appRetryDelay: porta occupata (di solito il servizio di prima non è ancora uscito): 1 s, 2 s, 5 s, poi ogni 10 s.
func appRetryDelay(n int) time.Duration {
	steps := []time.Duration{time.Second, 2 * time.Second, 5 * time.Second}
	if n >= 0 && n < len(steps) {
		return steps[n]
	}
	return 10 * time.Second
}

// scheduleRetry e stopRetry: con a.mu preso. Un solo tentativo in sospeso alla volta.
func (a *appService) scheduleRetry(d time.Duration) {
	if a.retry != nil {
		return
	}
	a.tries++
	a.retry = time.AfterFunc(d, func() {
		a.mu.Lock()
		a.retry = nil
		a.mu.Unlock()
		a.sync()
	})
}

func (a *appService) stopRetry() {
	if a.retry != nil {
		a.retry.Stop()
		a.retry = nil
	}
	a.tries = 0
}

func (a *appService) close() {
	a.mgr.Close()
	a.mu.Lock()
	a.closed = true
	a.stopRetry()
	if a.srv != nil {
		_ = a.srv.Close()
		a.srv, a.ln = nil, nil
	}
	if a.unannounce != nil {
		a.unannounce()
		a.unannounce = nil
	}
	a.mu.Unlock()
	// prima la porta, poi l'ultimo uso: nessun Touch si perde
	if err := a.mgr.Store.Flush(); err != nil {
		a.lg.Printf("telefoni: ultimo uso non salvato: %v", err)
	}
}

func (a *appService) addr() string {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.ln == nil {
		return ""
	}
	return a.ln.Addr().String()
}

// port: la porta in ascolto (nei test casuale), altrimenti quella della configurazione.
func (a *appService) port() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.ln != nil {
		return tcpPort(a.ln)
	}
	return a.cfgPort
}

// whoami: GET /app/whoami. Gli indirizzi di adesso: l'app li tiene per il prossimo cambio di rete.
func (a *appService) whoami(p api.PhoneInfo) any {
	return map[string]any{
		"phone": map[string]string{"id": p.ID, "name": p.Name},
		"pc":    map[string]string{"name": a.name, "version": a.version},
		"hosts": a.hosts(),
		"port":  a.port(),
	}
}

func (a *appService) PhonesView() api.PhonesView {
	v := api.PhonesView{Phones: []api.PhoneView{}}
	for _, p := range a.mgr.Store.List() {
		v.Phones = append(v.Phones, api.PhoneView{ID: p.ID, Name: p.Name, Paired: p.Paired, LastUsed: p.LastUsed})
	}
	a.mu.Lock()
	listening, lastErr := a.srv != nil, a.lastErr
	a.mu.Unlock()
	v.App = api.AppView{Port: a.port(), Listening: listening, Fingerprint: a.fp, Hosts: a.hosts()}
	if v.App.Hosts == nil {
		v.App.Hosts = []string{}
	}
	switch {
	case a.broken != nil:
		v.App.Error = a.broken.Error()
	case lastErr != nil:
		v.App.Error = lastErr.Error()
	}
	if code, exp, ok := a.mgr.Pairing.Active(); ok && listening {
		v.Pairing = &api.PairingView{Link: phones.Link(v.App.Hosts, v.App.Port, a.fp, code, a.name), Expires: exp.UnixMilli()}
	}
	return v
}

func (a *appService) OpenPairing() (api.PhonesView, error) {
	if a.broken != nil {
		return api.PhonesView{}, a.broken
	}
	if len(a.hosts()) == 0 {
		return api.PhonesView{}, errors.New("nessuna rete locale: collega il PC al Wi-Fi o al cavo")
	}
	a.mgr.OpenWindow() // → sync: la porta si accende qui
	if a.addr() == "" {
		a.mu.Lock()
		err := a.lastErr
		a.mu.Unlock()
		a.mgr.CancelWindow()
		if err == nil {
			err = fmt.Errorf("porta dei telefoni spenta")
		}
		return api.PhonesView{}, err
	}
	return a.PhonesView(), nil
}

func (a *appService) CancelPairing() api.PhonesView {
	a.mgr.CancelWindow()
	return a.PhonesView()
}

func (a *appService) RevokePhone(id string) error { return a.mgr.Revoke(id) }

// tcpPort: la porta di un ascoltatore (0 se non è TCP).
func tcpPort(ln net.Listener) int {
	if t, ok := ln.Addr().(*net.TCPAddr); ok {
		return t.Port
	}
	return 0
}

// PairingQR: il QR della finestra aperta (nil, false se chiusa o se il QR non si disegna).
func (a *appService) PairingQR() ([]byte, bool) {
	v := a.PhonesView()
	if v.Pairing == nil {
		return nil, false
	}
	svg, err := phones.QRSVG(v.Pairing.Link)
	if err != nil {
		a.lg.Printf("telefoni: QR non disegnato: %v", err)
		return nil, false
	}
	return svg, true
}
