// Esegue un caso del banco di parità con text.js della webapp; il risultato ha la stessa forma di quello di harness.cpp.
//   caso = [op, size, weight, pix, a1, a2, a3, a4, a5, stringa]  (op e argomenti come in harness.cpp)
import * as Tx from '../../../service/internal/webui/static/assets/js/theme/text.js';

// Surface che riassume i fillRect in un FNV-1a, come Rec in harness.cpp
function hashSurface() {
  let h = 2166136261, n = 0;
  const mix = (v) => { for (let k = 0; k < 4; k++) { h ^= (v >> (8 * k)) & 255; h = Math.imul(h, 16777619) >>> 0; } };
  return { fillRect(x, y, w, hh, ink) { n++; mix(x); mix(y); mix(w); mix(hh); mix(ink ? 1 : 0); }, result: () => [n, h] };
}

export function runJs(c) {
  const [op, size, weight, p, a1, a2, a3, a4, a5, s] = c;
  const pix = !!p;
  if (op === 'H') return Tx.hasFont(size, weight, pix) ? 1 : 0;
  if (op === 'W') return [Tx.textWidth(s, size, weight, pix), Tx.textWidthMilli(s, size, weight, pix)];
  if (op === 'F') return Tx.fitText(s, size, weight, pix, a1);
  if (op === 'L') return Tx.wrapLines(s, a1, size, a2, weight, pix);
  const surf = hashSurface();
  Tx.drawText(surf, s, a1, a2, size, { weight, pix, align: a3, ink: !!a4, max: a5 });
  return surf.result();
}
