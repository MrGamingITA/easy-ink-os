package autostart

import (
	"bytes"
	"encoding/xml"
	"fmt"
	"regexp"
	"strings"
)

func xmlText(s string) string {
	var b bytes.Buffer
	_ = xml.EscapeText(&b, []byte(s))
	return b.String()
}

// plist: LaunchAgent utente (launchd.plist(5)).
//   - RunAtLoad: parte quando viene caricato, cioè a ogni login e subito dopo `install`;
//   - KeepAlive/SuccessfulExit false: riparte solo se esce con un errore (porta occupata, crash), non
//     dopo uno spegnimento pulito (uscita 0 dopo SIGTERM);
//   - ThrottleInterval 30: al massimo un tentativo ogni 30 s;
//   - ExitTimeOut 15: secondi tra SIGTERM e SIGKILL (il servizio ne usa al massimo ~6 per bye);
//   - ProcessType Interactive: niente limiti di CPU e I/O, i hook di Claude Code aspettano il servizio;
//   - LimitLoadToSessionType Aqua: solo nella sessione grafica (non nelle sessioni ssh).
func (e Env) plist(args []string) []byte {
	var b strings.Builder
	w := func(format string, a ...any) { fmt.Fprintf(&b, format, a...) }
	w(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<!-- Scritto da «calamaio install» (Calamaio OS). Si toglie con «calamaio uninstall». -->
<plist version="1.0">
<dict>
	<key>Label</key>
	<string>%s</string>
	<key>ProgramArguments</key>
	<array>
`, Label)
	for _, a := range args {
		w("\t\t<string>%s</string>\n", xmlText(a))
	}
	w(`	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>KeepAlive</key>
	<dict>
		<key>SuccessfulExit</key>
		<false/>
	</dict>
	<key>ThrottleInterval</key>
	<integer>%d</integer>
	<key>ExitTimeOut</key>
	<integer>%d</integer>
	<key>ProcessType</key>
	<string>Interactive</string>
	<key>LimitLoadToSessionType</key>
	<string>Aqua</string>
	<key>WorkingDirectory</key>
	<string>%s</string>
	<key>EnvironmentVariables</key>
	<dict>
		<key>%s</key>
		<string>launchd</string>
		<key>PATH</key>
		<string>%s</string>
	</dict>
	<key>StandardOutPath</key>
	<string>%s</string>
	<key>StandardErrorPath</key>
	<string>%s</string>
</dict>
</plist>
`, RestartSec, StopTimeoutSec, xmlText(e.Home), EnvMarker, xmlText(e.pathEnv()), xmlText(e.LogFile()), xmlText(e.LogFile()))
	return []byte(b.String())
}

// systemdQuote: un argomento di ExecStart tra virgolette doppie (systemd.service(5), «Command
// lines»): \ e " con la barra, % raddoppiato (specificatori), $ raddoppiato (variabili).
func systemdQuote(s string) string {
	r := strings.NewReplacer(`\`, `\\`, `"`, `\"`, `%`, `%%`, `$`, `$$`)
	return `"` + r.Replace(s) + `"`
}

// envQuote: un'assegnazione di Environment= tra virgolette (qui $ non si espande, % sì).
func envQuote(s string) string {
	r := strings.NewReplacer(`\`, `\\`, `"`, `\"`, `%`, `%%`)
	return `"` + r.Replace(s) + `"`
}

// unit: unità systemd utente.
//   - Restart=on-failure, RestartSec=30: come KeepAlive/SuccessfulExit false su macOS;
//   - TimeoutStopSec=15: tra SIGTERM e SIGKILL;
//   - WantedBy=default.target: parte con la sessione dell'utente (systemd --user).
func (e Env) unit(args []string) ([]byte, error) {
	q := make([]string, len(args))
	for i, a := range args {
		if strings.ContainsAny(a, "\n\r") {
			return nil, fmt.Errorf("argomento con un a capo: %q", a)
		}
		q[i] = systemdQuote(a)
	}
	var b strings.Builder
	fmt.Fprintf(&b, `# Scritto da «calamaio install» (Calamaio OS). Si toglie con «calamaio uninstall».
[Unit]
Description=Calamaio OS: servizio per il display e-ink

[Service]
Type=simple
ExecStart=%s
Restart=on-failure
RestartSec=%d
TimeoutStopSec=%d
Environment=%s
`, strings.Join(q, " "), RestartSec, StopTimeoutSec, envQuote(EnvMarker+"=systemd"))
	fmt.Fprintf(&b, "Environment=%s\n", envQuote("PATH="+e.pathEnv()))
	// append: vuole un percorso senza spazi; con spazi il log resta nel journal (journalctl --user -u calamaio).
	if lf := e.LogFile(); !strings.ContainsAny(lf, " \t%") {
		fmt.Fprintf(&b, "StandardOutput=append:%s\nStandardError=append:%s\n", lf, lf)
	}
	b.WriteString(`
[Install]
WantedBy=default.target
`)
	return []byte(b.String()), nil
}

var (
	plistProg = regexp.MustCompile(`(?s)<key>ProgramArguments</key>\s*<array>\s*<string>(.*?)</string>`)
	unitProg  = regexp.MustCompile(`(?m)^ExecStart=(?:"((?:[^"\\]|\\.)*)"|(\S+))`)
)

// programOf: il binario indicato nel file (primo argomento).
func programOf(kind string, b []byte) string {
	switch kind {
	case "launchd":
		if m := plistProg.FindSubmatch(b); m != nil {
			var s string
			if err := xml.Unmarshal([]byte("<s>"+string(m[1])+"</s>"), &s); err == nil {
				return s
			}
			return string(m[1])
		}
	case "systemd":
		if m := unitProg.FindSubmatch(b); m != nil {
			if len(m[1]) > 0 {
				r := strings.NewReplacer(`\\`, `\`, `\"`, `"`, `%%`, `%`, `$$`, `$`)
				return r.Replace(string(m[1]))
			}
			return string(m[2])
		}
	}
	return ""
}
