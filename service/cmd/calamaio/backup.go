package main

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"text/tabwriter"
	"time"

	"calamaio/internal/backup"
	"calamaio/internal/config"
	"calamaio/internal/firmware"
)

// MAC nel nome del file, come in heltec-wireless-paper_aabbccdd48e8_fabbrica.bin.
var macInName = regexp.MustCompile(`(?i)(?:^|[_.-])([0-9a-f]{12})(?:[_.-]|$)`)

// factorySources: --factory-backup, poi factory_backups della config; se non c'è nessuno dei due,
// il default di sviluppo (backup-fabbrica/ del repository).
func factorySources(c config.Config, flagSrc backup.FactorySource) []backup.FactorySource {
	var out []backup.FactorySource
	if flagSrc.File != "" {
		flagSrc.File = absPath(flagSrc.File)
		if flagSrc.MAC == "" {
			if m := macInName.FindStringSubmatch(filepath.Base(flagSrc.File)); m != nil {
				flagSrc.MAC = m[1]
			}
		}
		out = append(out, flagSrc)
	}
	cfgDir, _ := config.Dir()
	for _, f := range c.FactoryBackups {
		file := f.File
		if file != "" && !filepath.IsAbs(file) && cfgDir != "" {
			file = filepath.Join(cfgDir, file)
		}
		out = append(out, backup.FactorySource{File: file, MAC: f.MAC, Chip: f.Chip, Board: f.Board, Name: f.Name,
			FlashSize: f.FlashSize, SHA256: f.SHA256, Note: f.Note})
	}
	if len(out) == 0 {
		out = backup.DevSources()
	}
	return out
}

func backupStore() (*backup.Store, error) {
	d, err := config.DataDir()
	if err != nil {
		return nil, err
	}
	return backup.Open(filepath.Join(d, "backups"))
}

const backupUsage = `uso:
  calamaio backup list
  calamaio backup import FILE --mac MAC [--name N] [--chip C] [--board B] [--flash-size N] [--offset N] [--note T] [--sha256 H]
  calamaio backup rm [--yes] ID        (l'originale di fabbrica e gli altri protetti non si eliminano)
`

// backupCmd: calamaio backup list | import | rm. Lavora direttamente sui file dell'archivio: il
// servizio, se è acceso, li rilegge a ogni richiesta e vede subito le modifiche.
func backupCmd(args []string, in io.Reader, out, errOut io.Writer) int {
	if len(args) == 0 {
		fmt.Fprint(errOut, backupUsage)
		return 2
	}
	bs, err := backupStore()
	if err != nil {
		fmt.Fprintln(errOut, "calamaio: archivio dei backup:", err)
		return 1
	}
	switch args[0] {
	case "list", "ls":
		return backupList(bs, out, errOut)
	case "import":
		return backupImport(bs, args[1:], out, errOut)
	case "rm", "delete":
		return backupRemove(bs, args[1:], in, out, errOut)
	default:
		fmt.Fprint(errOut, backupUsage)
		return 2
	}
}

func backupList(bs *backup.Store, out, errOut io.Writer) int {
	list, err := bs.List()
	if err != nil {
		fmt.Fprintln(errOut, "calamaio:", err)
		return 1
	}
	if len(list) == 0 {
		fmt.Fprintf(out, "Nessun backup in %s\n", bs.Dir)
		return 0
	}
	tw := tabwriter.NewWriter(out, 0, 0, 2, ' ', 0)
	fmt.Fprintln(tw, "ID\tTIPO\tDIMENSIONE\tREGIONE\tSCHEDA\tDATA\tSHA-256\tNOME")
	for _, b := range list {
		kind := "utente"
		if b.Kind == backup.KindFactory {
			kind = "fabbrica"
		}
		if b.Protected {
			kind += ", protetto"
		}
		who := strings.Trim(b.Board+" "+b.MAC, " ")
		fmt.Fprintf(tw, "%s\t%s\t%s\t0x%x+0x%x\t%s\t%s\t%s…\t%s\n", b.ID, kind, humanBytes(b.Size), b.Offset, b.Length,
			who, time.UnixMilli(b.Created).Local().Format("2006-01-02 15:04"), b.SHA256[:12], b.Name)
	}
	_ = tw.Flush()
	fmt.Fprintf(out, "%d backup in %s\n", len(list), bs.Dir)
	return 0
}

// parseInterspersed: flag prima e dopo gli argomenti posizionali (import FILE --mac …).
func parseInterspersed(fs *flag.FlagSet, args []string) ([]string, error) {
	var pos []string
	for {
		if err := fs.Parse(args); err != nil {
			return nil, err
		}
		if fs.NArg() == 0 {
			return pos, nil
		}
		pos = append(pos, fs.Arg(0))
		args = fs.Args()[1:]
	}
}

func backupImport(bs *backup.Store, args []string, out, errOut io.Writer) int {
	fs := flag.NewFlagSet("backup import", flag.ContinueOnError)
	fs.SetOutput(errOut)
	var m backup.Meta
	var flashSize, offset string
	fs.StringVar(&m.MAC, "mac", "", "MAC della scheda (obbligatorio)")
	fs.StringVar(&m.Name, "name", "", "nome del backup (vuoto = dal nome del file)")
	fs.StringVar(&m.Chip, "chip", "", "chip (vuoto = dall'intestazione dell'immagine)")
	fs.StringVar(&m.Board, "board", "", "scheda (vuoto = calamaio-xxxx dal MAC)")
	fs.StringVar(&flashSize, "flash-size", "", "dimensione della flash in byte (vuoto = sconosciuta)")
	fs.StringVar(&offset, "offset", "0", "indirizzo da cui parte il backup nella flash")
	fs.StringVar(&m.Note, "note", "", "nota")
	fs.StringVar(&m.SHA256, "sha256", "", "SHA-256 atteso (se c'è, deve coincidere)")
	pos, err := parseInterspersed(fs, args)
	if err != nil {
		return 2
	}
	if len(pos) != 1 || m.MAC == "" {
		fmt.Fprint(errOut, backupUsage)
		return 2
	}
	file := pos[0]
	if m.MAC, err = backup.NormalizeMAC(m.MAC); err == nil {
		if m.FlashSize, err = backup.ParseSize("flash-size", flashSize); err == nil {
			m.Offset, err = backup.ParseSize("offset", offset)
		}
	}
	if err != nil {
		fmt.Fprintln(errOut, "calamaio:", err)
		return 2
	}
	f, err := os.Open(file)
	if err != nil {
		fmt.Fprintln(errOut, "calamaio:", err)
		return 1
	}
	defer f.Close()
	if st, err := f.Stat(); err != nil || !st.Mode().IsRegular() {
		fmt.Fprintf(errOut, "calamaio: %s non è un file\n", file)
		return 1
	} else if st.Size() > backup.MaxSize {
		fmt.Fprintf(errOut, "calamaio: %s è troppo grande (%s, massimo %s)\n", file, humanBytes(st.Size()), humanBytes(backup.MaxSize))
		return 1
	}
	if m.Chip == "" {
		var hdr [24]byte
		if _, err := io.ReadFull(f, hdr[:]); err == nil {
			if c, err := firmware.ChipOfImage(hdr[:]); err == nil && !strings.HasPrefix(c, "chip ") {
				m.Chip = c
			}
		}
		if m.Chip == "" {
			fmt.Fprintln(errOut, "calamaio: chip non riconoscibile dal file: indicalo con --chip (es. ESP32-S3)")
			return 2
		}
		if _, err := f.Seek(0, io.SeekStart); err != nil {
			fmt.Fprintln(errOut, "calamaio:", err)
			return 1
		}
	}
	if m.Board == "" {
		m.Board = backup.BoardFromMAC(m.MAC)
	}
	if m.Name == "" {
		m.Name = "Importato · " + filepath.Base(file)
	}
	b, err := bs.Create(m, f)
	if err != nil {
		fmt.Fprintln(errOut, "calamaio: backup non importato:", err)
		return 1
	}
	fmt.Fprintf(out, "Importato %s «%s»: %s, SHA-256 %s\n", b.ID, b.Name, humanBytes(b.Size), b.SHA256)
	return 0
}

func backupRemove(bs *backup.Store, args []string, in io.Reader, out, errOut io.Writer) int {
	fs := flag.NewFlagSet("backup rm", flag.ContinueOnError)
	fs.SetOutput(errOut)
	yes := fs.Bool("yes", false, "non chiedere conferma")
	pos, err := parseInterspersed(fs, args)
	if err != nil {
		return 2
	}
	if len(pos) != 1 {
		fmt.Fprint(errOut, backupUsage)
		return 2
	}
	id := pos[0]
	b, err := bs.Get(id)
	if err != nil {
		fmt.Fprintf(errOut, "calamaio: %s: %v\n", id, err)
		return 1
	}
	if b.Protected || b.Kind == backup.KindFactory {
		fmt.Fprintf(errOut, "calamaio: «%s» è protetto: non si elimina\n", b.Name)
		return 1
	}
	if !*yes {
		fmt.Fprintf(out, "Elimino «%s» (%s, %s)? Non si annulla. Scrivi «si» per confermare: ", b.Name, b.ID, humanBytes(b.Size))
		line, _ := bufio.NewReader(in).ReadString('\n')
		if a := strings.ToLower(strings.TrimSpace(line)); a != "si" && a != "sì" {
			fmt.Fprintln(out, "Non eliminato.")
			return 1
		}
	}
	switch err := bs.Delete(id); {
	case errors.Is(err, backup.ErrProtected):
		fmt.Fprintf(errOut, "calamaio: «%s» è protetto: non si elimina\n", b.Name)
		return 1
	case err != nil:
		fmt.Fprintln(errOut, "calamaio:", err)
		return 1
	}
	fmt.Fprintf(out, "Eliminato %s «%s»\n", b.ID, b.Name)
	return 0
}

func humanBytes(n int64) string {
	switch {
	case n >= 1<<20 && n%(1<<20) == 0:
		return fmt.Sprintf("%d MiB", n>>20)
	case n >= 1<<20:
		return fmt.Sprintf("%.1f MiB", float64(n)/(1<<20))
	case n >= 1<<10:
		return fmt.Sprintf("%.1f KiB", float64(n)/(1<<10))
	}
	return fmt.Sprintf("%d B", n)
}
