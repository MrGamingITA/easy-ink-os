// Finto esptool-js per le prove con Playwright: shots.mjs lo serve AL POSTO di
// vendor/esptool-js-0.7.0/esptool.js (page.route) e finge navigator.serial. Non apre porte seriali.
// Simula un ESP32-S3 con 8 MB: tabella delle partizioni a 0x8000 (ota_0, ota_1) e otadata che
// fa partire ota_1. Il MAC si sceglie con globalThis.__FAKE_MAC.

const MB8 = 8 * 1024 * 1024;
let flash = null;

function crc32Rom(bytes) { // crc32_le della ROM chiamata con UINT32_MAX: stato interno da 0
  let c = 0;
  for (const x of bytes) { c ^= x; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; }
  return (~c) >>> 0;
}

function makeFlash() {
  const f = new Uint8Array(MB8).fill(0xff);
  const dv = new DataView(f.buffer);
  f[0] = 0xe9; f[12] = 9; // bootloader ESP32-S3
  const parts = [[1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'], [0, 0x10, 0x10000, 0x330000, 'app0'], [0, 0x11, 0x340000, 0x330000, 'app1'], [1, 0x82, 0x670000, 0x180000, 'spiffs']];
  parts.forEach(([type, sub, off, size, label], i) => {
    const o = 0x8000 + i * 32;
    dv.setUint16(o, 0x50AA, true); f[o + 2] = type; f[o + 3] = sub;
    dv.setUint32(o + 4, off, true); dv.setUint32(o + 8, size, true);
    for (let k = 0; k < 16; k++) f[o + 12 + k] = k < label.length ? label.charCodeAt(k) : 0;
    dv.setUint32(o + 28, 0, true);
  });
  // otadata: sequenza 1 nel primo settore, 2 nel secondo → parte app1 (ota_1)
  [[0xe000, 1], [0xf000, 2]].forEach(([o, seq]) => {
    dv.setUint32(o, seq, true);
    for (let k = 0; k < 20; k++) f[o + 4 + k] = 0xff;
    dv.setUint32(o + 24, 2, true); // ESP_OTA_IMG_VALID
    dv.setUint32(o + 28, crc32Rom(f.subarray(o, o + 4)), true);
  });
  f[0x10000] = 0xe9; f[0x10000 + 12] = 9;
  f[0x340000] = 0xe9; f[0x340000 + 12] = 9;
  return f;
}
const tick = (ms) => new Promise((r) => setTimeout(r, ms));

export class Transport {
  constructor(port) { this.port = port; }
  async disconnect() {}
}

export class ESPLoader {
  constructor(o) {
    this.terminal = o.terminal; this.transport = o.transport;
    this.chip = {
      CHIP_NAME: 'ESP32-S3', IMAGE_CHIP_ID: 9,
      getChipFeatures: async () => ['Wi-Fi', 'BLE', 'Embedded Flash 8MB (GD)'],
      getCrystalFreq: async () => 40,
      readMac: async () => globalThis.__FAKE_MAC || 'aa:bb:cc:dd:48:e8'
    };
  }
  log(s) { if (this.terminal) this.terminal.writeLine(s); }
  async main() { await tick(300); if (!flash) flash = makeFlash(); this.log('Chip is ESP32-S3 (QFN56) (revision v0.2) [finto]'); return 'ESP32-S3 (QFN56) (revision v0.2)'; }
  async readFlashId() { return 0x1740c8; }
  async detectFlashSize() { return '8MB'; }
  flashSizeBytes(s) { return parseInt(s, 10) * 1024 * 1024; }
  async readFlash(addr, size, cb) {
    const out = new Uint8Array(size), step = 0x20000;
    for (let got = 0; got < size;) {
      const n = Math.min(step, size - got);
      out.set(flash.subarray(addr + got, addr + got + n), got);
      got += n;
      await tick(4);
      if (cb) cb(out.subarray(got - n, got), got, size);
    }
    return out;
  }
  async writeFlash(o) {
    for (let i = 0; i < o.fileArray.length; i++) {
      const { data, address } = o.fileArray[i];
      const md5 = o.calculateMD5Hash ? o.calculateMD5Hash(data) : null;
      const total = Math.ceil(data.length / 3); // come se fosse compresso
      for (let k = 0; k <= 10; k++) { if (o.reportProgress) o.reportProgress(i, Math.round((total * k) / 10), total); await tick(40); }
      flash.set(data, address);
      this.log('Wrote ' + data.length + ' bytes at 0x' + address.toString(16) + ' [finto]');
      if (md5) { this.log('File  md5: ' + md5); this.log('Hash of data verified.'); }
    }
    this.log('Leaving...');
  }
  async after() { this.log('Hard resetting via RTS pin... [finto]'); }
}
export { makeFlash as _makeFlash };
