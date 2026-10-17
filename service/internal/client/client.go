// Package client contiene i comandi chiamati da Claude Code: hook e statusline.
// Regola d'oro: non disturbare mai Claude Code. Se il servizio non risponde si esce in silenzio.
package client

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os/exec"
	"runtime"
	"strings"
	"time"

	"calamaio/internal/config"
)

const maxStdin = 16 << 20

// httpClient: nessun timeout complessivo (lo decide il contesto di chi chiama), ma collegarsi al
// servizio locale deve riuscire subito: se non risponde, `hook` esce in silenzio.
var httpClient = &http.Client{Transport: &http.Transport{
	Proxy:               nil, // solo 127.0.0.1: mai un proxy
	DialContext:         (&net.Dialer{Timeout: 2 * time.Second}).DialContext,
	MaxIdleConns:        2,
	IdleConnTimeout:     30 * time.Second,
	DisableCompression:  true,
	TLSHandshakeTimeout: 2 * time.Second,
}}

func post(ctx context.Context, url string, body []byte) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	out, err := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if resp.StatusCode/100 != 2 {
		return nil, fmt.Errorf("stato %d", resp.StatusCode)
	}
	return out, err
}

func baseURL(c config.Config) string {
	return fmt.Sprintf("http://127.0.0.1:%d", c.APIPort)
}

// Hook inoltra lo stdin al servizio. Con wait aspetta una decisione (PermissionRequest)
// e la stampa su out solo se è JSON valido. Non restituisce mai errori: esce sempre con 0.
func Hook(c config.Config, stdin io.Reader, out io.Writer, wait bool) {
	body, err := io.ReadAll(io.LimitReader(stdin, maxStdin))
	if err != nil || len(body) == 0 {
		return
	}
	url := baseURL(c) + "/hook"
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	if wait {
		// Nessuna scadenza (DECISIONI, «Permessi senza scadenza»): il servizio tiene aperta la richiesta
		// finché il display risponde, oppure finché Claude Code chiude questo processo (risposta nel
		// terminale: il servizio vede la connessione chiusa). Il limite vero è il timeout del hook
		// nel plugin (86400 s). Se il servizio non c'è, il collegamento fallisce subito.
		ctx, cancel = context.WithCancel(context.Background())
		url += "?wait=1"
	}
	defer cancel()
	resp, err := post(ctx, url, body)
	if err != nil || !wait {
		return
	}
	resp = bytes.TrimSpace(resp)
	if len(resp) > 0 && json.Valid(resp) {
		_, _ = out.Write(resp)
		_, _ = out.Write([]byte("\n"))
	}
}

// StatusLine inoltra lo stdin (senza aspettare più di ~150 ms), poi stampa la status line
// precedente se c'era, altrimenti una riga sobria.
func StatusLine(c config.Config, stdin io.Reader, out io.Writer) {
	body, _ := io.ReadAll(io.LimitReader(stdin, maxStdin))

	done := make(chan struct{})
	go func() {
		defer close(done)
		if len(body) == 0 {
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), 150*time.Millisecond)
		defer cancel()
		_, _ = post(ctx, baseURL(c)+"/statusline", body)
	}()

	var line string
	if c.PreviousStatusLine != nil && strings.TrimSpace(c.PreviousStatusLine.Command) != "" {
		line = runPrevious(c.PreviousStatusLine.Command, body)
	} else {
		line = SoberLine(body)
	}
	<-done
	_, _ = io.WriteString(out, line)
}

func runPrevious(command string, stdin []byte) string {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	var cmd *exec.Cmd
	if runtime.GOOS == "windows" {
		cmd = exec.CommandContext(ctx, "cmd", "/C", command)
	} else {
		cmd = exec.CommandContext(ctx, "sh", "-c", command)
	}
	cmd.Stdin = bytes.NewReader(stdin)
	b, _ := cmd.Output()
	return string(b)
}

// SoberLine: nome sessione · contesto · 5h.
func SoberLine(body []byte) string {
	var p struct {
		SessionName string `json:"session_name"`
		Workspace   struct {
			CurrentDir string `json:"current_dir"`
		} `json:"workspace"`
		Context struct {
			Used *float64 `json:"used_percentage"`
		} `json:"context_window"`
		Rate struct {
			Five *struct {
				Used *float64 `json:"used_percentage"`
			} `json:"five_hour"`
		} `json:"rate_limits"`
	}
	_ = json.Unmarshal(body, &p)
	name := p.SessionName
	if name == "" {
		name = baseName(p.Workspace.CurrentDir)
	}
	if name == "" {
		name = "Calamaio"
	}
	parts := []string{name}
	if p.Context.Used != nil {
		parts = append(parts, fmt.Sprintf("ctx %.0f%%", *p.Context.Used))
	}
	if p.Rate.Five != nil && p.Rate.Five.Used != nil {
		parts = append(parts, fmt.Sprintf("5h %.0f%%", *p.Rate.Five.Used))
	}
	return strings.Join(parts, " · ") + "\n"
}

func baseName(p string) string {
	p = strings.TrimRight(strings.ReplaceAll(p, "\\", "/"), "/")
	if i := strings.LastIndex(p, "/"); i >= 0 {
		return p[i+1:]
	}
	return p
}
