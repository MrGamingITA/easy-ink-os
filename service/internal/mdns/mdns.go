// Package mdns annuncia il servizio: ai display `_calamaio._tcp` (TXT proto=1, path=/ws), ai telefoni
// abbinati `_calamaio-app._tcp` (TXT fp = prime 16 cifre dell'impronta), con un solo responder.
//
// Libreria: github.com/brutella/dnssd (responder mDNS/DNS-SD in Go puro, con probing e
// gestione dei conflitti di nome come da RFC 6762). Su macOS convive con mDNSResponder perché
// apre la porta 5353 con SO_REUSEPORT (lo fa la libreria standard di Go per gli indirizzi
// multicast).
package mdns

import (
	"context"
	"fmt"
	"log"
	"os"
	"strings"
	"sync"

	"github.com/brutella/dnssd"
	dnssdlog "github.com/brutella/dnssd/log"
)

const (
	ServiceType    = "_calamaio._tcp"
	AppServiceType = "_calamaio-app._tcp"
	// HostAlias è il nome host annunciato insieme al servizio: dà anche l'alias calamaio.local
	// citato nel brief (5.3). Se è già occupato la libreria passa a calamaio-2.local.
	HostAlias = "calamaio"
)

func init() {
	// La libreria scrive su stdout per default: i nostri log bastano.
	dnssdlog.Info.Disable()
}

// Responder: un solo responder per tutti gli annunci del servizio (due responder si contenderebbero
// calamaio.local).
type Responder struct {
	rp dnssd.Responder
	lg *log.Logger
}

func NewResponder(lg *log.Logger) (*Responder, error) {
	rp, err := dnssd.NewResponder()
	if err != nil {
		return nil, fmt.Errorf("mdns: %w", err)
	}
	return &Responder{rp: rp, lg: lg}, nil
}

// Run risponde finché ctx non viene chiuso. Blocca: va chiamato in una goroutine.
func (r *Responder) Run(ctx context.Context) error {
	err := r.rp.Respond(ctx)
	if ctx.Err() != nil {
		return nil
	}
	if err != nil {
		return fmt.Errorf("mdns: %w", err)
	}
	return nil
}

// Announce pubblica cfg, anche col responder già acceso (la libreria fa il probing, ~1 s: per questo in una
// goroutine), e restituisce la funzione che lo ritira. Non blocca.
func (r *Responder) Announce(cfg dnssd.Config) (stop func()) {
	var (
		mu      sync.Mutex
		h       dnssd.ServiceHandle
		stopped bool
	)
	go func() {
		sv, err := dnssd.NewService(cfg)
		if err == nil {
			var hh dnssd.ServiceHandle
			if hh, err = r.rp.Add(sv); err == nil {
				mu.Lock()
				defer mu.Unlock()
				if stopped {
					r.rp.Remove(hh)
					return
				}
				h = hh
				r.lg.Printf("mdns: annuncio %q %s porta %d (host %s.local)", cfg.Name, cfg.Type, cfg.Port, HostAlias)
				return
			}
		}
		r.lg.Printf("mdns: %s non annunciato: %v", cfg.Type, err)
	}()
	return func() {
		mu.Lock()
		defer mu.Unlock()
		stopped = true
		if h != nil {
			r.rp.Remove(h)
			h = nil
		}
	}
}

// DisplayConfig: l'annuncio per le schede.
func DisplayConfig(port int, path string) dnssd.Config {
	return dnssd.Config{Name: InstanceName(), Type: ServiceType, Host: HostAlias, Port: port,
		Text: map[string]string{"proto": "1", "path": path}}
}

// AppConfig: l'annuncio per l'app iPhone; nel TXT le prime 16 cifre dell'impronta (l'app riconosce il suo PC).
func AppConfig(port int, fp string) dnssd.Config {
	if len(fp) > 16 {
		fp = fp[:16]
	}
	return dnssd.Config{Name: InstanceName(), Type: AppServiceType, Host: HostAlias, Port: port,
		Text: map[string]string{"fp": fp}}
}

// InstanceName: «Calamaio su <nome del PC>», leggibile in un browser DNS-SD.
func InstanceName() string {
	return "Calamaio su " + ShortHost()
}

// ShortHost restituisce il nome del PC senza dominio (mac-sviluppo.local → mac-sviluppo).
func ShortHost() string {
	h, err := os.Hostname()
	if err != nil || h == "" {
		return "PC"
	}
	if i := strings.IndexByte(h, '.'); i > 0 {
		h = h[:i]
	}
	return h
}
