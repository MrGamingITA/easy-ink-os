package avatar

import (
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
)

// Formato binario CAVT v1 (docs/AVATAR.md §8), byte per byte come tools/avatar/lib/pack.mjs.
const (
	Magic  = "CAVT"
	Header = 72
)

var symbols = map[byte]byte{'.': 0, '#': 1, '+': 2}
var chars = []byte{'.', '#', '+'}

// FrameBytes: byte per fotogramma = ⌈w·h/4⌉.
func FrameBytes(w, h int) int { return (w*h + 3) / 4 }

// PackedSize: dimensione del binario.
func PackedSize(nPoses, frames, w, h int) int { return Header + nPoses*frames*FrameBytes(w, h) }

// Pack impacchetta un avatar valido nel binario CAVT v1 (con la posa run, bit 9, se c'è).
func Pack(a *Avatar) ([]byte, error) { return pack(a, Poses) }

// PackLegacy impacchetta senza la posa run: è il CAVT che accetta il firmware ≤ 0.6.0, che rifiuta il
// bit 9 di poseMask (docs/AVATAR.md §12.4). Senza run è identico a Pack.
func PackLegacy(a *Avatar) ([]byte, error) { return pack(a, LegacyPoses) }

func pack(a *Avatar, poses []string) ([]byte, error) {
	present := []string{}
	for _, p := range poses {
		if _, ok := a.Poses[p]; ok {
			present = append(present, p)
		}
	}
	mask := 0
	for i, p := range poses {
		if _, ok := a.Poses[p]; ok {
			mask |= 1 << i
		}
	}
	fb := FrameBytes(a.W, a.H)
	total := PackedSize(len(present), a.Frames, a.W, a.H)
	if total > 0xFFFF {
		return nil, fmt.Errorf("binario di %d byte: non sta nel campo di 16 bit", total)
	}
	buf := make([]byte, total)
	copy(buf, Magic)
	buf[4] = Version
	buf[5] = Header
	binary.LittleEndian.PutUint16(buf[6:], uint16(total))
	buf[12], buf[13], buf[14], buf[15] = byte(a.W), byte(a.H), byte(a.Frames), 0
	binary.LittleEndian.PutUint16(buf[16:], uint16(mask))
	binary.LittleEndian.PutUint16(buf[18:], uint16(fb))
	if len(a.ID) > 24 || len(a.Name) > 24 {
		return nil, errors.New("id o name oltre i 24 byte del campo")
	}
	copy(buf[20:44], a.ID)
	copy(buf[44:68], a.Name)
	off := Header
	for _, p := range present {
		for _, fr := range a.Poses[p] {
			i := 0
			for _, row := range fr {
				for k := 0; k < len(row); k++ {
					code, ok := symbols[row[k]]
					if !ok {
						return nil, fmt.Errorf("simbolo %q non ammesso", row[k])
					}
					buf[off+(i>>2)] |= code << (6 - 2*(i&3)) // pixel 0 nei bit 7-6
					i++
				}
			}
			off += fb
		}
	}
	binary.LittleEndian.PutUint32(buf[8:], crc32.ChecksumIEEE(buf[12:total]))
	return buf, nil
}

// CRC restituisce il CRC-32 scritto nell'intestazione.
func CRC(bin []byte) uint32 {
	if len(bin) < 12 {
		return 0
	}
	return binary.LittleEndian.Uint32(bin[8:])
}

// Unpack decodifica e verifica un binario CAVT (stessi controlli di unpack() in Node).
func Unpack(buf []byte) (*Avatar, error) {
	if len(buf) < Header || string(buf[:4]) != Magic {
		return nil, errors.New("magia CAVT assente")
	}
	if buf[4] != Version {
		return nil, fmt.Errorf("versione %d non supportata", buf[4])
	}
	total := int(binary.LittleEndian.Uint16(buf[6:]))
	if total != len(buf) {
		return nil, fmt.Errorf("lunghezza dichiarata %d, reale %d", total, len(buf))
	}
	if binary.LittleEndian.Uint32(buf[8:]) != crc32.ChecksumIEEE(buf[12:total]) {
		return nil, errors.New("checksum errato")
	}
	w, h, frames := int(buf[12]), int(buf[13]), int(buf[14])
	mask := int(binary.LittleEndian.Uint16(buf[16:]))
	fb := int(binary.LittleEndian.Uint16(buf[18:]))
	if fb != FrameBytes(w, h) {
		return nil, errors.New("frameBytes incoerente")
	}
	for i := range Required {
		if mask&(1<<i) == 0 {
			return nil, fmt.Errorf("posa obbligatoria assente: %s", Poses[i])
		}
	}
	if mask>>len(Poses) != 0 {
		return nil, errors.New("bit di posa sconosciuti")
	}
	var present []string
	for i, p := range Poses {
		if mask&(1<<i) != 0 {
			present = append(present, p)
		}
	}
	if total != PackedSize(len(present), frames, w, h) {
		return nil, errors.New("dimensione incoerente con pose e fotogrammi")
	}
	a := &Avatar{ID: cstr(buf[20:44]), Name: cstr(buf[44:68]), W: w, H: h, Frames: frames, Poses: map[string][][]string{}}
	off := int(buf[5])
	for _, p := range present {
		for f := 0; f < frames; f++ {
			rows := make([]string, h)
			for y := 0; y < h; y++ {
				row := make([]byte, w)
				for x := 0; x < w; x++ {
					i := y*w + x
					v := (buf[off+(i>>2)] >> (6 - 2*(i&3))) & 3
					if v == 3 {
						return nil, errors.New("codice di pixel 3 riservato")
					}
					row[x] = chars[v]
				}
				rows[y] = string(row)
			}
			a.Poses[p] = append(a.Poses[p], rows)
			off += fb
		}
	}
	return a, nil
}

func cstr(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}
