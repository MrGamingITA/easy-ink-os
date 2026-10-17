// QR come firmware/src/gfx/qr.cpp + gfx/qr/qrcode_ricmoo.cpp (ricmoo/QRCode, licenza MIT: vedi
// firmware/src/gfx/qr/qrcode_ricmoo.LICENSE.txt). Solo versioni 1–5, correzione M. Stessa scelta del modo e della maschera.
// Porting funzione per funzione: stessi nomi in camelCase, uint8_t → & 0xff dove il C++ tronca.

const ECC_MEDIUM = 1;  // qrcode_ricmoo.h: ECC_LOW 0, ECC_MEDIUM 1, ECC_QUARTILE 2, ECC_HIGH 3
const MODE_NUMERIC = 0, MODE_ALPHANUMERIC = 1, MODE_BYTE = 2;

// Tabelle di qrcode_ricmoo.cpp, solo le versioni 1–5. Righe per eccFormatBits: Medium, Low, High, Quartile.
const NUM_ERROR_CORRECTION_CODEWORDS = [[10, 16, 26, 36, 48], [7, 10, 15, 20, 26], [17, 28, 44, 64, 88], [13, 22, 36, 52, 72]];
const NUM_ERROR_CORRECTION_BLOCKS = [[1, 1, 1, 2, 2], [1, 1, 1, 1, 1], [1, 1, 2, 4, 4], [1, 1, 2, 2, 4]];
const NUM_RAW_DATA_MODULES = [208, 359, 567, 807, 1079];
// formato a 2 bit per ciascun livello: ECC_FORMAT_BITS >> (2 * ecc)
const ECC_FORMAT_BITS = (0x02 << 6) | (0x03 << 4) | (0x00 << 2) | (0x01 << 0);

// I byte arrivano come uint8_t; il C++ li legge come char (con segno): i byte ≥ 0x80 sono negativi.
const sch = (b) => (b << 24) >> 24;

function getAlphanumeric(c) {
  if (c >= 0x30 && c <= 0x39) return c - 0x30;
  if (c >= 0x41 && c <= 0x5a) return c - 0x41 + 10;
  switch (c) {
    case 0x20: return 36; case 0x24: return 37; case 0x25: return 38; case 0x2a: return 39; case 0x2b: return 40;
    case 0x2d: return 41; case 0x2e: return 42; case 0x2f: return 43; case 0x3a: return 44;
  }
  return -1;
}
function isAlphanumeric(text, length) {
  while (length !== 0) if (getAlphanumeric(sch(text[--length])) === -1) return false;
  return true;
}
function isNumeric(text, length) {
  while (length !== 0) { const c = sch(text[--length]); if (c < 0x30 || c > 0x39) return false; }
  return true;
}

// Bit della lunghezza per modo (impacchettati meno 8 in modeInfo, come nel C++)
function getModeBits(version, mode) {
  let modeInfo = 0x7bbb80a;
  if (version > 9) modeInfo >>>= 9;
  if (version > 26) modeInfo >>>= 9;
  let result = 8 + ((modeInfo >>> (3 * mode)) & 0x07);
  if (result === 15) result = 16;
  return result;
}

// BitBucket: { bitOffsetOrWidth, capacityBytes, data }
const bbGetGridSizeBytes = (size) => ((size * size + 7) / 8) | 0;
const bbGetBufferSizeBytes = (bits) => ((bits + 7) / 8) | 0;
function bbInitBuffer(capacityBytes) { return { bitOffsetOrWidth: 0, capacityBytes, data: new Uint8Array(capacityBytes) }; }
function bbInitGrid(data, size) { data.fill(0); return { bitOffsetOrWidth: size, capacityBytes: bbGetGridSizeBytes(size), data }; }
function bbAppendBits(b, val, length) {
  let offset = b.bitOffsetOrWidth;
  for (let i = length - 1; i >= 0; i--, offset++) b.data[offset >> 3] |= ((val >>> i) & 1) << (7 - (offset & 7));
  b.bitOffsetOrWidth = offset;
}
function bbSetBit(g, x, y, on) {
  const offset = y * g.bitOffsetOrWidth + x, mask = 1 << (7 - (offset & 7));
  if (on) g.data[offset >> 3] |= mask; else g.data[offset >> 3] &= ~mask;
}
function bbInvertBit(g, x, y, invert) {
  const offset = y * g.bitOffsetOrWidth + x, mask = 1 << (7 - (offset & 7));
  const on = (g.data[offset >> 3] & mask) !== 0;
  if (on !== invert) g.data[offset >> 3] |= mask; else g.data[offset >> 3] &= ~mask;
}
function bbGetBit(g, x, y) {
  const offset = y * g.bitOffsetOrWidth + x;
  return (g.data[offset >> 3] & (1 << (7 - (offset & 7)))) !== 0;
}

// XOR dei moduli di dati con la maschera: due volte la stessa maschera = nessun cambiamento
function applyMask(modules, isFunction, mask) {
  const size = modules.bitOffsetOrWidth;
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    if (bbGetBit(isFunction, x, y)) continue;
    let invert = false;
    switch (mask) {
      case 0: invert = (x + y) % 2 === 0; break;
      case 1: invert = y % 2 === 0; break;
      case 2: invert = x % 3 === 0; break;
      case 3: invert = (x + y) % 3 === 0; break;
      case 4: invert = (((x / 3) | 0) + ((y / 2) | 0)) % 2 === 0; break;
      case 5: invert = ((x * y) % 2) + ((x * y) % 3) === 0; break;
      case 6: invert = (((x * y) % 2) + ((x * y) % 3)) % 2 === 0; break;
      case 7: invert = (((x + y) % 2) + ((x * y) % 3)) % 2 === 0; break;
    }
    bbInvertBit(modules, x, y, invert);
  }
}
function setFunctionModule(modules, isFunction, x, y, on) {
  bbSetBit(modules, x, y, on);
  bbSetBit(isFunction, x, y, true);
}
// cercatore 9×9 col separatore, centro in (x, y)
function drawFinderPattern(modules, isFunction, x, y) {
  const size = modules.bitOffsetOrWidth;
  for (let i = -4; i <= 4; i++) for (let j = -4; j <= 4; j++) {
    const dist = Math.max(Math.abs(i), Math.abs(j)), xx = x + j, yy = y + i;
    if (0 <= xx && xx < size && 0 <= yy && yy < size) setFunctionModule(modules, isFunction, xx, yy, dist !== 2 && dist !== 4);
  }
}
// allineamento 5×5, centro in (x, y)
function drawAlignmentPattern(modules, isFunction, x, y) {
  for (let i = -2; i <= 2; i++) for (let j = -2; j <= 2; j++)
    setFunctionModule(modules, isFunction, x + j, y + i, Math.max(Math.abs(i), Math.abs(j)) !== 1);
}
function drawFormatBits(modules, isFunction, ecc, mask) {
  const size = modules.bitOffsetOrWidth;
  let data = (ecc << 3) | mask, rem = data;
  for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
  data = ((data << 10) | rem) ^ 0x5412;
  for (let i = 0; i <= 5; i++) setFunctionModule(modules, isFunction, 8, i, ((data >>> i) & 1) !== 0);
  setFunctionModule(modules, isFunction, 8, 7, ((data >>> 6) & 1) !== 0);
  setFunctionModule(modules, isFunction, 8, 8, ((data >>> 7) & 1) !== 0);
  setFunctionModule(modules, isFunction, 7, 8, ((data >>> 8) & 1) !== 0);
  for (let i = 9; i < 15; i++) setFunctionModule(modules, isFunction, 14 - i, 8, ((data >>> i) & 1) !== 0);
  for (let i = 0; i <= 7; i++) setFunctionModule(modules, isFunction, size - 1 - i, 8, ((data >>> i) & 1) !== 0);
  for (let i = 8; i < 15; i++) setFunctionModule(modules, isFunction, 8, size - 15 + i, ((data >>> i) & 1) !== 0);
  setFunctionModule(modules, isFunction, 8, size - 8, true);
}
// drawVersion del C++ non fa niente sotto la versione 7: qui non serve
function drawFunctionPatterns(modules, isFunction, version, ecc) {
  const size = modules.bitOffsetOrWidth;
  for (let i = 0; i < size; i++) {
    setFunctionModule(modules, isFunction, 6, i, i % 2 === 0);
    setFunctionModule(modules, isFunction, i, 6, i % 2 === 0);
  }
  drawFinderPattern(modules, isFunction, 3, 3);
  drawFinderPattern(modules, isFunction, size - 4, 3);
  drawFinderPattern(modules, isFunction, 3, size - 4);
  if (version > 1) {
    const alignCount = ((version / 7) | 0) + 2;
    const step = version !== 32 ? (((version * 4 + alignCount * 2 + 1) / (2 * alignCount - 2)) | 0) * 2 : 26;
    const alignPosition = new Array(alignCount);
    let alignPositionIndex = alignCount - 1;
    alignPosition[0] = 6;
    for (let i = 0, pos = version * 4 + 17 - 7; i < alignCount - 1; i++, pos -= step) alignPosition[alignPositionIndex--] = pos;
    for (let i = 0; i < alignCount; i++) for (let j = 0; j < alignCount; j++) {
      if ((i === 0 && j === 0) || (i === 0 && j === alignCount - 1) || (i === alignCount - 1 && j === 0)) continue;
      drawAlignmentPattern(modules, isFunction, alignPosition[i], alignPosition[j]);
    }
  }
  drawFormatBits(modules, isFunction, ecc, 0);  // maschera provvisoria, riscritta dopo
}
// codeword a zigzag nell'area dati
function drawCodewords(modules, isFunction, codewords) {
  const bitLength = codewords.bitOffsetOrWidth, data = codewords.data, size = modules.bitOffsetOrWidth;
  let i = 0;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (let vert = 0; vert < size; vert++) for (let j = 0; j < 2; j++) {
      const x = right - j, upwards = ((right & 2) === 0) !== (x < 6), y = upwards ? size - 1 - vert : vert;
      if (!bbGetBit(isFunction, x, y) && i < bitLength) {
        bbSetBit(modules, x, y, ((data[i >> 3] >> (7 - (i & 7))) & 1) !== 0);
        i++;
      }
    }
  }
}

const PENALTY_N1 = 3, PENALTY_N2 = 3, PENALTY_N3 = 40, PENALTY_N4 = 10;
function getPenaltyScore(modules) {
  let result = 0;
  const size = modules.bitOffsetOrWidth;
  for (let y = 0; y < size; y++) {
    let colorX = bbGetBit(modules, 0, y);
    for (let x = 1, runX = 1; x < size; x++) {
      const cx = bbGetBit(modules, x, y);
      if (cx !== colorX) { colorX = cx; runX = 1; } else { runX++; if (runX === 5) result += PENALTY_N1; else if (runX > 5) result++; }
    }
  }
  for (let x = 0; x < size; x++) {
    let colorY = bbGetBit(modules, x, 0);
    for (let y = 1, runY = 1; y < size; y++) {
      const cy = bbGetBit(modules, x, y);
      if (cy !== colorY) { colorY = cy; runY = 1; } else { runY++; if (runY === 5) result += PENALTY_N1; else if (runY > 5) result++; }
    }
  }
  let black = 0;
  for (let y = 0; y < size; y++) {
    let bitsRow = 0, bitsCol = 0;
    for (let x = 0; x < size; x++) {
      const color = bbGetBit(modules, x, y);
      if (x > 0 && y > 0) {
        const colorUL = bbGetBit(modules, x - 1, y - 1), colorUR = bbGetBit(modules, x, y - 1), colorL = bbGetBit(modules, x - 1, y);
        if (color === colorUL && color === colorUR && color === colorL) result += PENALTY_N2;
      }
      // come nel C++: la colonna legge (y, x), cioè la trasposta
      bitsRow = ((bitsRow << 1) & 0x7ff) | (color ? 1 : 0);
      bitsCol = ((bitsCol << 1) & 0x7ff) | (bbGetBit(modules, y, x) ? 1 : 0);
      if (x >= 10) {
        if (bitsRow === 0x05d || bitsRow === 0x5d0) result += PENALTY_N3;
        if (bitsCol === 0x05d || bitsCol === 0x5d0) result += PENALTY_N3;
      }
      if (color) black++;
    }
  }
  const total = size * size;
  for (let k = 0; black * 20 < (9 - k) * total || black * 20 > (11 + k) * total; k++) result += PENALTY_N4;
  return result;
}

// Reed-Solomon su GF(2^8/0x11D)
function rsMultiply(x, y) {
  let z = 0;
  for (let i = 7; i >= 0; i--) {
    z = ((z << 1) ^ ((z >> 7) * 0x11d)) & 0xffff;
    z ^= ((y >> i) & 1) * x;
  }
  return z & 0xff;
}
function rsInit(degree) {
  const coeff = new Uint8Array(degree);
  coeff[degree - 1] = 1;
  let root = 1;
  for (let i = 0; i < degree; i++) {
    for (let j = 0; j < degree; j++) {
      coeff[j] = rsMultiply(coeff[j], root);
      if (j + 1 < degree) coeff[j] ^= coeff[j + 1];
    }
    root = ((root << 1) ^ ((root >> 7) * 0x11d)) & 0xffff;
  }
  return coeff;
}
// resto scritto in result[base + j * stride]
function rsGetRemainder(degree, coeff, data, dOff, length, result, base, stride) {
  for (let i = 0; i < length; i++) {
    const factor = data[dOff + i] ^ result[base];
    for (let j = 1; j < degree; j++) result[base + (j - 1) * stride] = result[base + j * stride];
    result[base + (degree - 1) * stride] = 0;
    for (let j = 0; j < degree; j++) result[base + j * stride] ^= rsMultiply(coeff[j], factor);
  }
}

function encodeDataCodewords(dataCodewords, text, length, version) {
  let mode = MODE_BYTE;
  if (isNumeric(text, length)) {
    mode = MODE_NUMERIC;
    bbAppendBits(dataCodewords, 1 << MODE_NUMERIC, 4);
    bbAppendBits(dataCodewords, length, getModeBits(version, MODE_NUMERIC));
    let accumData = 0, accumCount = 0;
    for (let i = 0; i < length; i++) {
      accumData = accumData * 10 + (text[i] - 0x30);
      if (++accumCount === 3) { bbAppendBits(dataCodewords, accumData, 10); accumData = 0; accumCount = 0; }
    }
    if (accumCount > 0) bbAppendBits(dataCodewords, accumData, accumCount * 3 + 1);
  } else if (isAlphanumeric(text, length)) {
    mode = MODE_ALPHANUMERIC;
    bbAppendBits(dataCodewords, 1 << MODE_ALPHANUMERIC, 4);
    bbAppendBits(dataCodewords, length, getModeBits(version, MODE_ALPHANUMERIC));
    let accumData = 0, accumCount = 0;
    for (let i = 0; i < length; i++) {
      accumData = accumData * 45 + getAlphanumeric(text[i]);
      if (++accumCount === 2) { bbAppendBits(dataCodewords, accumData, 11); accumData = 0; accumCount = 0; }
    }
    if (accumCount > 0) bbAppendBits(dataCodewords, accumData, 6);
  } else {
    bbAppendBits(dataCodewords, 1 << MODE_BYTE, 4);
    bbAppendBits(dataCodewords, length, getModeBits(version, MODE_BYTE));
    for (let i = 0; i < length; i++) bbAppendBits(dataCodewords, text[i], 8);
  }
  return mode;
}

function performErrorCorrection(version, ecc, data) {
  const numBlocks = NUM_ERROR_CORRECTION_BLOCKS[ecc][version - 1];
  const totalEcc = NUM_ERROR_CORRECTION_CODEWORDS[ecc][version - 1];
  const moduleCount = NUM_RAW_DATA_MODULES[version - 1];
  const blockEccLen = (totalEcc / numBlocks) | 0;
  const numShortBlocks = numBlocks - ((((moduleCount / 8) | 0) % numBlocks));
  const shortBlockLen = ((((moduleCount / 8) | 0) / numBlocks) | 0);
  const shortDataBlockLen = shortBlockLen - blockEccLen;
  const result = new Uint8Array(data.capacityBytes);
  const coeff = rsInit(blockEccLen);
  let offset = 0;
  const dataBytes = data.data;
  // blocchi corti, intercalati
  for (let i = 0; i < shortDataBlockLen; i++) {
    let index = i, stride = shortDataBlockLen;
    for (let blockNum = 0; blockNum < numBlocks; blockNum++) {
      result[offset++] = dataBytes[index];
      if (blockNum === numShortBlocks) stride++;
      index += stride;
    }
  }
  // blocchi lunghi
  {
    let index = shortDataBlockLen * (numShortBlocks + 1), stride = shortDataBlockLen;
    for (let blockNum = 0; blockNum < numBlocks - numShortBlocks; blockNum++) {
      result[offset++] = dataBytes[index];
      if (blockNum === 0) stride++;
      index += stride;
    }
  }
  // correzione d'errore di ogni blocco, intercalata
  let blockSize = shortDataBlockLen, dOff = 0;
  for (let blockNum = 0; blockNum < numBlocks; blockNum++) {
    if (blockNum === numShortBlocks) blockSize++;
    rsGetRemainder(blockEccLen, coeff, dataBytes, dOff, blockSize, result, offset + blockNum, numBlocks);
    dOff += blockSize;
  }
  data.data.set(result);
  data.bitOffsetOrWidth = moduleCount;
}

// qrcode_initBytes: moduli in modules (Uint8Array di qrcodeGetBufferSize(version) byte); -1 se il modo non va
export function qrcodeInitBytes(modules, version, ecc, data, length) {
  if (version < 1 || version > 5) return null;  // le tabelle qui coprono solo 1–5
  const size = version * 4 + 17;
  const eccFormatBits = (ECC_FORMAT_BITS >> (2 * ecc)) & 0x03;
  const moduleCount = NUM_RAW_DATA_MODULES[version - 1];
  const dataCapacity = ((moduleCount / 8) | 0) - NUM_ERROR_CORRECTION_CODEWORDS[eccFormatBits][version - 1];
  const codewords = bbInitBuffer(bbGetBufferSizeBytes(moduleCount));
  const mode = encodeDataCodewords(codewords, data, length, version);
  if (mode < 0) return null;
  // terminatore e riempimento fino al byte
  let padding = dataCapacity * 8 - codewords.bitOffsetOrWidth;
  if (padding > 4) padding = 4;
  bbAppendBits(codewords, 0, padding);
  bbAppendBits(codewords, 0, (8 - (codewords.bitOffsetOrWidth % 8)) % 8);
  for (let padByte = 0xec; codewords.bitOffsetOrWidth < dataCapacity * 8; padByte ^= 0xec ^ 0x11) bbAppendBits(codewords, padByte, 8);

  const modulesGrid = bbInitGrid(modules, size);
  const isFunctionGrid = bbInitGrid(new Uint8Array(bbGetGridSizeBytes(size)), size);
  drawFunctionPatterns(modulesGrid, isFunctionGrid, version, eccFormatBits);
  performErrorCorrection(version, eccFormatBits, codewords);
  drawCodewords(modulesGrid, isFunctionGrid, codewords);
  // maschera con la penalità più bassa (la prima a parità)
  let mask = 0, minPenalty = 0x7fffffff;
  for (let i = 0; i < 8; i++) {
    drawFormatBits(modulesGrid, isFunctionGrid, eccFormatBits, i);
    applyMask(modulesGrid, isFunctionGrid, i);
    const penalty = getPenaltyScore(modulesGrid);
    if (penalty < minPenalty) { mask = i; minPenalty = penalty; }
    applyMask(modulesGrid, isFunctionGrid, i);
  }
  drawFormatBits(modulesGrid, isFunctionGrid, eccFormatBits, mask);
  applyMask(modulesGrid, isFunctionGrid, mask);
  return { version, size, ecc, mode, mask, modules };
}
// qrcode_initText: il testo come byte UTF-8, fino al primo NUL come strlen
export function qrcodeInitText(modules, version, ecc, bytes) {
  const n = bytes.indexOf(0);
  return qrcodeInitBytes(modules, version, ecc, bytes, n < 0 ? bytes.length : n);
}
export const qrcodeGetBufferSize = (version) => bbGetGridSizeBytes(4 * version + 17);
export function qrcodeGetModule(q, x, y) {
  if (x < 0 || x >= q.size || y < 0 || y >= q.size) return false;
  const offset = y * q.size + x;
  return (q.modules[offset >> 3] & (1 << (7 - (offset & 7)))) !== 0;
}

// qr::encode del firmware: versione più piccola (1–5) che contiene il testo in modo byte, livello M.
const CAP_M = [14, 26, 42, 62, 84];  // ISO/IEC 18004, tabella 7: modo byte, livello M
export const MAX_VERSION = 5;
export function encode(text) {
  let bytes = new TextEncoder().encode(String(text));
  const nul = bytes.indexOf(0);
  if (nul >= 0) bytes = bytes.subarray(0, nul);  // strlen
  for (let v = 1; v <= MAX_VERSION; v++) {
    if (bytes.length > CAP_M[v - 1]) continue;
    const q = qrcodeInitText(new Uint8Array(qrcodeGetBufferSize(v)), v, ECC_MEDIUM, bytes);
    if (!q) return null;
    return { size: q.size, version: v, mask: q.mask, modules: q.modules, get: (x, y) => qrcodeGetModule(q, x, y) };
  }
  return null;
}
// moduli impacchettati come il buffer di ricmoo (bit 7 = primo modulo), in esadecimale: per il confronto col C++
export const modulesHex = (q) => Array.from(q.modules, (b) => b.toString(16).padStart(2, '0')).join('');
