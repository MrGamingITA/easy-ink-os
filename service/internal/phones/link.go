package phones

import (
	"fmt"
	"net"
	"net/url"
	"sort"
	"strings"
)

// Link: il contenuto del QR di abbinamento (spec §3.2). Gli spazi del nome sono %20: URLComponents di iOS
// non legge «+» come spazio.
func Link(hosts []string, port int, fp, code, name string) string {
	n := strings.ReplaceAll(url.QueryEscape(name), "+", "%20")
	return fmt.Sprintf("calamaio://pair?v=1&h=%s&p=%d&fp=%s&c=%s&n=%s", strings.Join(hosts, ","), port, fp, code, n)
}

// LocalIPv4s: gli indirizzi IPv4 del PC per il QR (interfacce accese, niente loopback né 169.254/16),
// prima quelli privati, al massimo 4.
func LocalIPv4s() []string {
	ifs, _ := net.Interfaces()
	var out []string
	for _, ifc := range ifs {
		if ifc.Flags&net.FlagUp == 0 || ifc.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := ifc.Addrs()
		for _, a := range addrs {
			ipn, ok := a.(*net.IPNet)
			if !ok {
				continue
			}
			ip := ipn.IP.To4()
			if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
				continue
			}
			out = append(out, ip.String())
		}
	}
	sort.SliceStable(out, func(i, j int) bool {
		return net.ParseIP(out[i]).IsPrivate() && !net.ParseIP(out[j]).IsPrivate()
	})
	if len(out) > 4 {
		out = out[:4]
	}
	return out
}
