package display

import "time"

// byeCmd: `bye` messo in coda alla connessione e scritto dal suo goroutine (run), così arriva dopo
// ogni `state` già partito e nessuno `state` lo segue. done riceve l'esito della scrittura.
type byeCmd struct {
	msg  any
	done chan error
}

// Bye manda msg (il messaggio `bye`) a tutti i display che hanno fatto hello e aspetta le scritture
// al massimo wait. Da qui in poi l'hub non manda più `state` e rifiuta le connessioni nuove: resta
// solo da chiudere (Close). Va chiamata dopo StopPermissions, così i `perm-close service` partono
// prima. Restituisce quanti display l'hanno ricevuto e a quanti si è provato a mandarlo.
func (h *Hub) Bye(msg any, wait time.Duration) (sent, total int) {
	var cmds []*byeCmd
	h.mu.Lock()
	h.farewell = true
	for _, c := range h.conns {
		if !c.info.Hello {
			continue
		}
		b := &byeCmd{msg: msg, done: make(chan error, 1)}
		select {
		case c.out <- b:
			cmds = append(cmds, b)
		default:
			h.Log.Printf("ws: #%d coda piena: bye non mandato", c.info.ID)
		}
		total++
	}
	h.mu.Unlock()
	deadline := time.NewTimer(wait)
	defer deadline.Stop()
	for _, b := range cmds {
		select {
		case err := <-b.done:
			if err == nil {
				sent++
			}
		case <-deadline.C:
			h.Log.Printf("servizio in arresto: bye mandato a %d display su %d (gli altri non hanno risposto entro %s)", sent, total, wait)
			return sent, total
		}
	}
	h.Log.Printf("servizio in arresto: bye mandato a %d display su %d", sent, total)
	return sent, total
}

// farewellSet dice se Bye è già partito.
func (h *Hub) farewellSet() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.farewell
}
