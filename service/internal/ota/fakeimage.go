package ota

import (
	"crypto/sha256"
	"encoding/binary"
)

// FakeImage costruisce un'immagine dell'app ESP32-S3 finta ma ben formata, per i test: intestazione,
// descrittore dell'app con l'impronta di elf (se non nil), le stringhe C della versione e del profilo
// (come le lascia il compilatore in .rodata) e riempitivo fino a size byte.
func FakeImage(version, device string, size int, elf []byte) []byte {
	if size < 512 {
		size = 512
	}
	b := make([]byte, size)
	b[0] = 0xE9
	b[1] = 3                                    // segmenti
	binary.LittleEndian.PutUint16(b[12:], 0x09) // ESP32-S3
	binary.LittleEndian.PutUint32(b[appDescOff:], appDescMagic)
	copy(b[appDescOff+16:], "esp-idf: v4.4.7")
	copy(b[appDescOff+112:], "v4.4.7-dirty")
	if elf != nil {
		sum := sha256.Sum256(elf)
		copy(b[appDescOff+144:], sum[:])
	}
	off := 256
	for _, s := range []string{version, device} {
		if s == "" {
			continue
		}
		off++ // byte 0 prima
		copy(b[off:], s)
		off += len(s) + 1 // e dopo
	}
	for i := off + 16; i < size; i++ {
		b[i] = byte(i * 7)
		if b[i] == 0 {
			b[i] = 0x5A
		}
	}
	return b
}
