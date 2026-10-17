package fake

import (
	"fmt"

	"calamaio/internal/state"
)

// ctx è ciò che vede un copione a ogni passo.
type ctx struct {
	s   *state.Snapshot
	ss  *state.Session
	now int64
	k   int // passo del copione, da 0
	run *run
}

// run: avanzamento del copione di una sessione.
type run struct {
	k    int // passi fatti
	mark int // passo in cui è successa l'ultima cosa da aspettare (fine, errore)
}

func (c *ctx) markNow()         { c.run.mark = c.k }
func (c *ctx) after(n int) bool { return c.k-c.run.mark >= n }

// Un copione restituisce true quando è finito: la sessione riparte dall'esempio.
type script func(c *ctx) bool

var scripts = map[string]script{
	"a1": apiServer,
	"b2": webApp,
	"c3": firmware,
	"d4": docsSite,
}

func (c *ctx) set(st, detail string) {
	if c.ss.State != st {
		c.ss.Since = c.now
	}
	c.ss.State = st
	c.ss.Detail = detail
	c.ss.Error = ""
	if st == state.Permission || st == state.Input {
		if c.ss.AttnAt == nil {
			c.ss.AttnAt = &c.now
		}
	} else {
		c.ss.AttnAt = nil
	}
}

func (c *ctx) log(text string) { c.s.AddLog(c.now, c.ss.Name, text) }

func (c *ctx) addCtx(n int) {
	v := n
	if c.ss.Ctx != nil {
		v += *c.ss.Ctx
	}
	if v > 99 {
		v = 99
	}
	c.ss.Ctx = &v
}

func (c *ctx) work(add, del int, cost float64) {
	c.s.Today.LinesAdd += add
	c.s.Today.LinesDel += del
	c.s.Today.Cost = float64(int((c.s.Today.Cost+cost)*100+0.5)) / 100
}

// advance chiude il task in corso e avvia il successivo; restituisce fatti, totale.
func (c *ctx) advance() (int, int) {
	t := c.ss.Tasks
	for i := range t {
		if t[i].Status == state.TaskDoing {
			t[i].Status = state.TaskDoneSt
			c.s.Today.Tasks++
			break
		}
	}
	for i := range t {
		if t[i].Status == state.TaskTodo {
			t[i].Status = state.TaskDoing
			break
		}
	}
	done := 0
	for _, x := range t {
		if x.Status == state.TaskDoneSt {
			done++
		}
	}
	if done < len(t) {
		c.log(fmt.Sprintf("task %d/%d completato", done, len(t)))
	}
	return done, len(t)
}

// resolvePerm chiude un'attesa di permesso e aggiorna le statistiche di Oggi. Fino a M5 il
// display non risponde ai permessi: nel copione si risolvono sempre dal terminale.
func (c *ctx) resolvePerm() {
	if c.ss.AttnAt != nil {
		wait := c.now - *c.ss.AttnAt
		n := int64(c.s.Today.PermDisplay + c.s.Today.PermTerminal)
		c.s.Today.WaitAvgMs = (c.s.Today.WaitAvgMs*n + wait) / (n + 1)
	}
	c.s.Today.PermTerminal++
	c.log("permesso concesso dal terminale")
}

// api-server: lavora, completa un task ogni 3 passi, finisce con un riepilogo e riparte.
var apiDetails = []string{"Edit tests/orders.test.ts", "Bash: npm run test", "Edit src/routes/orders.ts",
	"Read src/schemas/order.ts", "Edit openapi.yaml", `Grep "status(4"`, "Edit src/errors.ts", "Read src/routes/orders.ts"}

func apiServer(c *ctx) bool {
	ss := c.ss
	if ss.State == state.Done {
		return c.after(3) // dopo 3 passi di riposo, nuovo prompt
	}
	c.set(state.Working, apiDetails[c.k%len(apiDetails)])
	c.addCtx(2)
	c.work(14, 3, 0.05)
	if ss.Detail == "Bash: npm run test" {
		ss.Test = strp("run")
	} else if ss.Test != nil && *ss.Test == "run" {
		ss.Test = strp("ok")
	}
	ss.Subs = []int{2, 2, 1, 1, 0, 1}[c.k%6]
	if c.k%3 == 2 {
		if done, total := c.advance(); done == total {
			c.set(state.Done, fmt.Sprintf("%d task completati", total))
			ss.Summary = "Endpoint ordini pronto: test verdi e OpenAPI aggiornata."
			ss.Test = strp("ok")
			ss.Subs = 0
			c.log("ha finito")
			c.markNow()
		}
	}
	return false
}

// web-app: aspetta un permesso, lo ottiene dal terminale, i test falliscono e poi passano, chiede
// un altro permesso (sempre dal terminale), finisce e dopo un po' riparte dall'esempio.
func webApp(c *ctx) bool {
	ss := c.ss
	switch c.k {
	case 0: // ancora in attesa del permesso dell'esempio
	case 1:
		c.resolvePerm()
		c.set(state.Working, "Bash: npm run test")
		ss.Test = strp("run")
	case 2:
		ss.Test = strp("ko")
		c.set(state.Working, "Edit src/components/Cart.tsx")
		c.advance()
		c.log("test falliti: 2")
		c.work(22, 9, 0.06)
	case 3:
		c.set(state.Working, "Edit src/hooks/useCart.ts")
		c.addCtx(3)
		c.work(8, 4, 0.04)
	case 4:
		c.set(state.Working, "Bash: npm run test")
		ss.Test = strp("run")
	case 5:
		ss.Test = strp("ok")
		c.set(state.Working, "Read package.json")
		c.advance()
		c.log("test superati")
		for i := range c.s.PRs {
			if c.s.PRs[i].Repo == "web-app" {
				c.s.PRs[i].Checks = "ok"
			}
		}
	case 6:
		c.set(state.Permission, "Bash: rm -rf build")
		c.log("chiede permesso: rm -rf build")
	case 7, 8:
	case 9:
		c.resolvePerm()
		c.set(state.Working, "Bash: npm run build")
		ss.Subs = 1
	case 10:
		c.set(state.Working, "Bash: npx lighthouse http://localhost:3000")
		c.advance()
		c.addCtx(4)
	case 11:
		c.set(state.Working, "Edit CHANGELOG.md")
		ss.Subs = 0
		c.work(6, 0, 0.03)
	case 12:
		c.advance()
		c.set(state.Done, "9 task completati")
		ss.Summary = "Carrello mobile completato: test verdi, Lighthouse 96."
		c.log("ha finito")
	case 13, 14, 15:
	default:
		return true
	}
	return false
}

// firmware-esp32: legge e modifica file, il contesto cresce fino alla compattazione, prende
// un errore «overloaded» e riprende, alla fine conclude con un riepilogo.
var fwDetails = []string{"Read lib/epd/driver.cpp", "Edit src/ui/layout.cpp", "Bash: pio run",
	"Edit src/ui/sprites.cpp", "Bash: pio test -e native", "Read src/net/ws.cpp"}

func firmware(c *ctx) bool {
	ss := c.ss
	switch ss.State {
	case state.Done:
		return c.after(4)
	case state.Compact:
		c.set(state.Working, fwDetails[c.k%len(fwDetails)])
		ss.Ctx = intp(22)
		c.log("contesto compattato")
		return false
	case state.Error:
		if c.after(2) {
			c.set(state.Working, "Edit src/net/ws.cpp")
			c.log("riprende dopo l'errore")
		}
		return false
	}
	if ss.Ctx != nil && *ss.Ctx >= 80 {
		c.set(state.Compact, "Compattazione del contesto")
		c.log("compatta il contesto")
		return false
	}
	c.set(state.Working, fwDetails[c.k%len(fwDetails)])
	c.addCtx(7)
	c.work(10, 2, 0.04)
	switch ss.Detail {
	case "Bash: pio test -e native":
		ss.Test = strp("run")
	default:
		if ss.Test != nil && *ss.Test == "run" {
			ss.Test = strp("ok")
		}
	}
	ss.Subs = []int{1, 1, 0, 2, 1, 0}[c.k%6]
	if c.k%4 == 3 {
		done, total := c.advance()
		switch {
		case done == total:
			c.set(state.Done, fmt.Sprintf("%d task completati", total))
			ss.Summary = "Client WebSocket pronto e sprite della mascotte disegnati."
			ss.Subs = 0
			c.log("ha finito")
			c.markNow()
		case done == 3:
			c.set(state.Error, "API sovraccarica, riprovo")
			ss.Error = "overloaded"
			c.log("errore: overloaded")
			c.markNow()
		}
	}
	return false
}

// docs-site: finita, poi inattiva, poi fa una domanda, lavora su due task e rifinisce.
func docsSite(c *ctx) bool {
	ss := c.ss
	switch c.k {
	case 0, 1, 2, 3:
	case 4:
		c.set(state.Idle, "")
		c.log("inattiva")
	case 5:
	case 6:
		c.set(state.Input, "Domanda: quale versione documentare?")
		ss.Summary = ""
		c.log("aspetta una risposta")
	case 7:
	case 8:
		c.set(state.Working, "Edit docs/versioni.md")
		ss.Summary = ""
		ss.Tasks = []state.Task{{Title: "Pagina versioni", Status: 1}, {Title: "Note di rilascio", Status: 0}}
		ss.Ctx = intp(14)
		c.log("nuovo prompt")
	case 9:
		c.set(state.Working, "Edit docs/release-notes.md")
		c.advance()
		c.addCtx(6)
		c.work(40, 5, 0.03)
	case 10:
		c.advance()
		c.set(state.Done, "2 task completati")
		ss.Summary = "Pagina delle versioni e note di rilascio 0.4 pubblicate."
		c.log("ha finito")
	case 11, 12, 13:
	default:
		return true
	}
	return false
}

// global: limiti, servizi e PR che cambiano piano piano.
var apiMs = []int{85, 92, 78, 110, 88, 95, 81}

func global(s *state.Snapshot, tick int, now int64) {
	if l := s.Limits; l != nil && tick%3 == 0 {
		if l.H5 != nil {
			v := *l.H5 + 1
			if v > 98 {
				v = 8
			}
			l.H5 = &v
		}
		if l.D7 != nil && tick%9 == 0 {
			v := *l.D7 + 1
			if v > 98 {
				v = 5
			}
			l.D7 = &v
		}
	}
	for i := range s.Services {
		sv := &s.Services[i]
		switch sv.Name {
		case "API":
			sv.Ms = apiMs[tick%len(apiMs)]
		case "Backup notturno":
			if tick%8 == 0 {
				sv.OK = !sv.OK
				sv.Ms = map[bool]int{true: 340, false: 0}[sv.OK]
				text := "servizio giù"
				if sv.OK {
					text = "servizio di nuovo su"
				}
				s.AddLog(now, sv.Name, text)
			}
		}
	}
	for i := range s.PRs {
		pr := &s.PRs[i]
		if pr.Checks == "run" && tick%5 == 0 {
			pr.Checks = "ok"
		} else if pr.Checks == "ok" && pr.Repo == "api-server" && tick%15 == 0 {
			pr.Checks = "run"
		}
	}
}
