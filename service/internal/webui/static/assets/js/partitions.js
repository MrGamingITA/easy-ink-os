// Tabella delle partizioni ESP-IDF (a 0x8000) e otadata: quale app avvia la scheda.
// Formato di una voce (32 byte): magic 0x50AA, tipo, sottotipo, offset u32, size u32, etichetta 16 byte, flag u32.
// otadata: due copie (una per settore da 0x1000) di { ota_seq u32, seq_label 20 byte, ota_state u32, crc u32 }.

export const PART_TABLE_OFFSET = 0x8000;
export const PART_TABLE_SIZE = 0xC00;
const SECTOR = 0x1000;

export function parsePartitionTable(b) {
  const out = [];
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  for (let o = 0; o + 32 <= b.length; o += 32) {
    const magic = dv.getUint16(o, true);
    if (magic === 0xEBEB || magic === 0xFFFF) break; // voce MD5 o fine tabella
    if (magic !== 0x50AA) return out.length ? out : null;
    let label = '';
    for (let i = 0; i < 16 && b[o + 12 + i]; i++) label += String.fromCharCode(b[o + 12 + i]);
    out.push({ type: b[o + 2], subtype: b[o + 3], offset: dv.getUint32(o + 4, true), size: dv.getUint32(o + 8, true), label });
  }
  return out.length ? out : null;
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
// crc32_le della ROM: crc = ~crc all'inizio e alla fine. ESP-IDF chiama crc32_le(UINT32_MAX, &ota_seq, 4),
// quindi lo stato interno parte da 0. Si accetta anche la variante zlib (stato iniziale 0xFFFFFFFF) per prudenza.
function crc32(bytes, init) {
  let c = init >>> 0;
  for (const x of bytes) c = CRC_TABLE[(c ^ x) & 0xff] ^ (c >>> 8);
  return (~c) >>> 0;
}
export function otaEntryValid(seqBytes, crc) { return crc === crc32(seqBytes, 0) || crc === crc32(seqBytes, 0xFFFFFFFF); }

// Restituisce { part, reason } con la partizione app che il bootloader avvia.
export function activeApp(parts, otadata) {
  const apps = parts.filter((p) => p.type === 0x00);
  if (!apps.length) return null;
  const factory = apps.find((p) => p.subtype === 0x00);
  const otas = apps.filter((p) => p.subtype >= 0x10 && p.subtype < 0x20).sort((a, b) => a.subtype - b.subtype);
  if (!otas.length) return { part: factory || apps[0], reason: 'nessuna partizione OTA: si avvia ' + (factory || apps[0]).label };
  let best = null;
  if (otadata) {
    const dv = new DataView(otadata.buffer, otadata.byteOffset, otadata.byteLength);
    for (const o of [0, SECTOR]) {
      if (o + 32 > otadata.length) continue;
      const seq = dv.getUint32(o, true), state = dv.getUint32(o + 24, true), crc = dv.getUint32(o + 28, true);
      if (seq === 0xFFFFFFFF || seq === 0) continue;
      if (state === 3 || state === 4) continue; // ESP_OTA_IMG_INVALID / ABORTED
      if (!otaEntryValid(otadata.subarray(o, o + 4), crc)) continue;
      if (!best || seq > best) best = seq;
    }
  }
  if (best) {
    const part = otas[(best - 1) % otas.length];
    return { part, reason: 'otadata: sequenza ' + best + ' → ' + part.label };
  }
  const part = factory || otas[0];
  return { part, reason: 'otadata vuota: si avvia ' + part.label };
}
