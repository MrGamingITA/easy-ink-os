package ota

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	"calamaio/internal/firmware"
)

// DefaultMaxApp: dimensione di una partizione app della tabella del firmware (firmware/partitions_gb.csv, con i
// giochi Game Boy: app0 e app1 da 0x240000 = 2 359 296 byte, come partitions.bin della build heltec213).
// CheckImage la usa sempre, Resolve quando accanto all'immagine manca partitions.bin.
const DefaultMaxApp = 0x240000

// ImageInfo: quello che si legge dall'intestazione di un'immagine dell'app ESP-IDF.
type ImageInfo struct {
	Chip      string // dall'intestazione (esp_image_header_t.chip_id)
	ElfSHA256 string // esp_app_desc_t.app_elf_sha256: SHA-256 di firmware.elf, se l'immagine lo porta
	IDF       string // esp_app_desc_t.idf_ver
}

// Offset di esp_app_desc_t: intestazione dell'immagine (24 byte) + intestazione del primo segmento (8).
const appDescOff = 32
const appDescMagic = 0xABCD5432

// ParseImage controlla che b sia un'immagine dell'app (magia 0xE9, descrittore dell'app) e ne legge chip
// e impronta dell'ELF.
func ParseImage(b []byte) (ImageInfo, error) {
	var info ImageInfo
	if len(b) < 256 {
		return info, fmt.Errorf("immagine troppo corta (%d byte)", len(b))
	}
	chip, err := firmware.ChipOfImage(b[:24])
	if err != nil {
		return info, err
	}
	info.Chip = chip
	if binary.LittleEndian.Uint32(b[appDescOff:]) != appDescMagic {
		return info, errors.New("non è l'immagine di un'app (manca il descrittore dell'app: bootloader o immagine completa della flash?)")
	}
	info.IDF = cstr(b[appDescOff+112 : appDescOff+144])
	sum := b[appDescOff+144 : appDescOff+176]
	if !bytes.Equal(sum, make([]byte, 32)) {
		info.ElfSHA256 = hex.EncodeToString(sum)
	}
	return info, nil
}

func cstr(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		b = b[:i]
	}
	return string(b)
}

// HasCString: s compare nell'immagine come stringa C intera (preceduta e seguita da un byte 0).
func HasCString(img []byte, s string) bool {
	if s == "" {
		return false
	}
	return bytes.Contains(img, append(append([]byte{0}, s...), 0))
}

// markerRe: stringa che il firmware può includere per dichiarare versione e profilo senza ambiguità,
// «calamaio-fw <versione> <device>» (docs/PROTOCOLLO.md, «OTA»).
var markerRe = regexp.MustCompile(`\x00calamaio-fw ([0-9A-Za-z][0-9A-Za-z.+_-]{0,30}) ([a-z0-9][a-z0-9_-]{0,30})\x00`)

// Build è un'immagine pronta da offrire, con l'origine di ogni dato (per dirlo all'utente).
type Build struct {
	Bin        []byte
	Path       string // firmware.bin
	Dir        string
	Version    string
	VersionSrc string
	Device     string
	DeviceSrc  string
	Built      int64 // ms epoch
	SHA256     string
	Info       ImageInfo
	ELF        string // firmware.elf accanto, se c'è e corrisponde all'immagine
	ELFNote    string // perché non c'è, se non c'è
	MaxSize    int64
}

// ResolveOptions: da dove leggere e cosa forzare.
type ResolveOptions struct {
	Path       string // firmware.bin, oppure la cartella della build (firmware/.pio/build/<env>)
	Version    string // --version: sostituisce le altre fonti (deve comunque comparire nell'immagine)
	Device     string // --device: idem
	DeviceHint string // profilo della scheda di destinazione: usato solo se compare nell'immagine
}

// Resolve legge l'immagine e ricava versione e profilo. Ordine delle fonti per la versione:
// --version, stringa «calamaio-fw V D» nell'immagine, manifest.json del pacchetto, file «version»
// accanto, firmware/src/core/model.h se la cartella è una build del repository (con la stringa
// «calamaio-fw» le altre fonti, model.h escluso, devono dire la stessa cosa). Tranne la stringa
// «calamaio-fw», ogni versione trovata deve comparire nell'immagine come stringa C intera: così una
// build vecchia con i sorgenti già cambiati non passa per quella nuova.
// Profilo: --device, «calamaio-fw», manifest.json (`device`), nome della cartella .pio/build/<env>, il
// profilo della scheda; anche qui deve comparire nell'immagine.
func Resolve(o ResolveOptions) (*Build, error) {
	if o.Path == "" {
		return nil, errors.New("indica il file firmware.bin o la cartella della build")
	}
	path := o.Path
	st, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if st.IsDir() {
		path = filepath.Join(path, "firmware.bin")
		if st, err = os.Stat(path); err != nil {
			return nil, fmt.Errorf("nella cartella %s manca firmware.bin", o.Path)
		}
	}
	abs, err := filepath.Abs(path)
	if err == nil {
		path = abs
	}
	b := &Build{Path: path, Dir: filepath.Dir(path), Built: st.ModTime().UnixMilli()}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	b.MaxSize = maxAppSize(b.Dir)
	b.Bin, err = io.ReadAll(io.LimitReader(f, b.MaxSize+1))
	f.Close()
	if err != nil {
		return nil, err
	}
	if int64(len(b.Bin)) > b.MaxSize {
		return nil, fmt.Errorf("%s è più grande della partizione dell'app (%d byte al massimo)", path, b.MaxSize)
	}
	if b.Info, err = ParseImage(b.Bin); err != nil {
		return nil, fmt.Errorf("%s: %v", path, err)
	}
	sum := sha256.Sum256(b.Bin)
	b.SHA256 = hex.EncodeToString(sum[:])

	var man struct {
		Version string `json:"version"`
		Device  string `json:"device"`
	}
	if mb, err := os.ReadFile(filepath.Join(b.Dir, "manifest.json")); err == nil {
		_ = json.Unmarshal(mb, &man)
	}
	marker := markerRe.FindSubmatch(b.Bin)

	// versione
	type cand struct{ v, src string }
	var vs []cand
	if o.Version != "" {
		vs = append(vs, cand{o.Version, "--version"})
	}
	if marker != nil {
		vs = append(vs, cand{string(marker[1]), "stringa calamaio-fw nell'immagine"})
	}
	if man.Version != "" {
		vs = append(vs, cand{man.Version, "manifest.json"})
	}
	if vb, err := os.ReadFile(filepath.Join(b.Dir, "version")); err == nil {
		vs = append(vs, cand{strings.TrimSpace(string(vb)), "file version"})
	}
	if v := firmware.DevVersion(b.Dir); v != "" {
		vs = append(vs, cand{v, "firmware/src/core/model.h"})
	}
	if len(vs) == 0 {
		return nil, errors.New("non so che versione è: aggiungi un file «version» accanto a firmware.bin oppure usa --version")
	}
	c := vs[0]
	if marker != nil && c.src == "--version" && c.v != string(marker[1]) {
		return nil, fmt.Errorf("--version %s non coincide con la versione scritta nell'immagine (%s)", c.v, marker[1])
	}
	if marker != nil && len(vs) > 1 {
		for _, o := range vs[1:] {
			if o.src != "firmware/src/core/model.h" && o.v != string(marker[1]) {
				return nil, fmt.Errorf("versioni discordanti: %s dice %s, l'immagine dice %s", o.src, o.v, marker[1])
			}
		}
	}
	if !versionRe.MatchString(c.v) {
		return nil, fmt.Errorf("versione %q (%s) non valida", c.v, c.src)
	}
	if c.src != "stringa calamaio-fw nell'immagine" && !HasCString(b.Bin, c.v) {
		return nil, fmt.Errorf("la versione %s (da %s) non compare nell'immagine: firmware.bin è di una build diversa? Ricompila o indica la versione giusta con --version", c.v, c.src)
	}
	b.Version, b.VersionSrc = c.v, c.src

	// profilo
	var ds []cand
	if o.Device != "" {
		ds = append(ds, cand{o.Device, "--device"})
	}
	if marker != nil {
		ds = append(ds, cand{string(marker[2]), "stringa calamaio-fw nell'immagine"})
	}
	if man.Device != "" {
		ds = append(ds, cand{man.Device, "manifest.json"})
	}
	if env := pioEnv(b.Dir); env != "" {
		ds = append(ds, cand{env, "cartella della build PlatformIO"})
	}
	if o.DeviceHint != "" {
		ds = append(ds, cand{o.DeviceHint, "profilo della scheda"})
	}
	if len(ds) == 0 {
		return nil, errors.New("non so per quale scheda è l'immagine: usa --device (es. heltec213)")
	}
	d := ds[0]
	if !deviceRe.MatchString(d.v) {
		return nil, fmt.Errorf("profilo %q (%s) non valido", d.v, d.src)
	}
	if d.src != "stringa calamaio-fw nell'immagine" && !HasCString(b.Bin, d.v) {
		return nil, fmt.Errorf("il profilo %s (da %s) non compare nell'immagine: non sembra un firmware per questa scheda", d.v, d.src)
	}
	b.Device, b.DeviceSrc = d.v, d.src

	// ELF della stessa build (serve a tradurre gli indirizzi dei crash)
	elf := filepath.Join(b.Dir, "firmware.elf")
	switch sum, err := fileSHA256(elf); {
	case err != nil:
		b.ELFNote = "nessun firmware.elf accanto all'immagine"
	case b.Info.ElfSHA256 == "":
		b.ELFNote = "l'immagine non porta l'impronta dell'ELF"
	case sum != b.Info.ElfSHA256:
		b.ELFNote = "firmware.elf accanto è di un'altra build"
	default:
		b.ELF = elf
	}
	return b, nil
}

// pioEnv: nome dell'ambiente se dir è firmware/.pio/build/<env> (l'ambiente ha il nome del profilo).
func pioEnv(dir string) string {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return ""
	}
	if filepath.Base(filepath.Dir(abs)) == "build" && filepath.Base(filepath.Dir(filepath.Dir(abs))) == ".pio" {
		return filepath.Base(abs)
	}
	return ""
}

// maxAppSize: la partizione app più piccola di partitions.bin accanto (l'immagine deve stare in tutte e
// due), altrimenti DefaultMaxApp.
func maxAppSize(dir string) int64 {
	b, err := os.ReadFile(filepath.Join(dir, "partitions.bin"))
	if err != nil {
		return DefaultMaxApp
	}
	var min int64
	for i := 0; i+32 <= len(b); i += 32 {
		e := b[i : i+32]
		if e[0] != 0xAA || e[1] != 0x50 {
			break
		}
		if e[2] == 0 && (e[3] == 0x10 || e[3] == 0x11) { // app, ota_0 / ota_1
			sz := int64(binary.LittleEndian.Uint32(e[8:12]))
			if min == 0 || sz < min {
				min = sz
			}
		}
	}
	if min == 0 {
		return DefaultMaxApp
	}
	return min
}

func fileSHA256(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// ---------- archivio degli ELF ----------

// ElfArchiveMax: quanti ELF tenere (i più recenti).
const ElfArchiveMax = 20

var unsafeName = regexp.MustCompile(`[^0-9A-Za-z.+_-]`)

// ArchiveELF copia firmware.elf in dir come <versione>_<sha256>.elf, se non c'è già, e tiene solo gli
// ultimi ElfArchiveMax. Serve a tradurre gli indirizzi di un crash di una versione già installata anche
// quando la build è cambiata.
func ArchiveELF(dir, elfPath, version, elfSHA string) (string, error) {
	if elfPath == "" || len(elfSHA) != 64 {
		return "", nil
	}
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return "", err
	}
	name := unsafeName.ReplaceAllString(version, "_") + "_" + elfSHA + ".elf"
	dst := filepath.Join(dir, name)
	if _, err := os.Stat(dst); err == nil {
		now := time.Now()
		_ = os.Chtimes(dst, now, now)
		return dst, nil
	}
	src, err := os.Open(elfPath)
	if err != nil {
		return "", err
	}
	defer src.Close()
	tmp := dst + ".tmp"
	out, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o600)
	if err != nil {
		return "", err
	}
	h := sha256.New()
	if _, err := io.Copy(io.MultiWriter(out, h), src); err != nil {
		out.Close()
		os.Remove(tmp)
		return "", err
	}
	if err := out.Close(); err != nil {
		os.Remove(tmp)
		return "", err
	}
	if hex.EncodeToString(h.Sum(nil)) != elfSHA {
		os.Remove(tmp)
		return "", errors.New("firmware.elf è cambiato durante la copia")
	}
	if err := os.Rename(tmp, dst); err != nil {
		os.Remove(tmp)
		return "", err
	}
	pruneELF(dir)
	return dst, nil
}

func pruneELF(dir string) {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return
	}
	type it struct {
		name string
		t    time.Time
	}
	var list []it
	for _, e := range ents {
		if !strings.HasSuffix(e.Name(), ".elf") {
			continue
		}
		if fi, err := e.Info(); err == nil {
			list = append(list, it{e.Name(), fi.ModTime()})
		}
	}
	if len(list) <= ElfArchiveMax {
		return
	}
	sort.Slice(list, func(i, j int) bool { return list[i].t.After(list[j].t) })
	for _, x := range list[ElfArchiveMax:] {
		os.Remove(filepath.Join(dir, x.name))
	}
}

// ArchivedELF è un ELF dell'archivio.
type ArchivedELF struct {
	Path    string
	Version string
	SHA256  string
}

// ListELF elenca l'archivio.
func ListELF(dir string) []ArchivedELF {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var out []ArchivedELF
	for _, e := range ents {
		n := e.Name()
		if !strings.HasSuffix(n, ".elf") {
			continue
		}
		base := strings.TrimSuffix(n, ".elf")
		i := strings.LastIndexByte(base, '_')
		if i < 0 || len(base)-i-1 != 64 {
			continue
		}
		out = append(out, ArchivedELF{Path: filepath.Join(dir, n), Version: base[:i], SHA256: base[i+1:]})
	}
	return out
}

// CheckImage controlla un'immagine arrivata con versione e profilo già decisi (calamaio ota push la
// manda al servizio così): app valida, dimensione, versione e profilo presenti nell'immagine.
func CheckImage(bin []byte, version, device string) (ImageInfo, error) {
	info, err := ParseImage(bin)
	if err != nil {
		return info, err
	}
	if len(bin) > DefaultMaxApp {
		return info, fmt.Errorf("immagine di %d byte: più grande della partizione dell'app (%d)", len(bin), DefaultMaxApp)
	}
	if !versionRe.MatchString(version) || !deviceRe.MatchString(device) {
		return info, errors.New("versione o profilo non validi")
	}
	if m := markerRe.FindSubmatch(bin); m != nil {
		if string(m[1]) != version || string(m[2]) != device {
			return info, fmt.Errorf("l'immagine dice versione %s per %s, non %s per %s", m[1], m[2], version, device)
		}
		return info, nil
	}
	if !HasCString(bin, version) {
		return info, fmt.Errorf("la versione %s non compare nell'immagine", version)
	}
	if !HasCString(bin, device) {
		return info, fmt.Errorf("il profilo %s non compare nell'immagine", device)
	}
	return info, nil
}
