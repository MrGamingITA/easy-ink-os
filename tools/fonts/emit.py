"""Seconda metà di gen.py: dai dati del browser ai file C++ (più un confronto di fedeltà in Python)."""
import json
import math

W, H = 250, 122


def unhex(h, n):
    bits = []
    for ch in h:
        v = int(ch, 16)
        bits += [(v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1]
    return bits[:n]


def pack(bits):
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            out[i >> 3] |= 0x80 >> (i & 7)
    return bytes(out)


def units(adv_px, size):
    """Avanzamento in millesimi di em (UPM 1000 per Chakra Petch e Silkscreen)."""
    return round(adv_px * 1000 / size)


# ---------------------------------------------------------------- emulazione Python del renderer C++
class PyFont:
    def __init__(self, f, kern):
        self.size, self.weight, self.pix = f["size"], f["weight"], f["pix"]
        self.g = {g["cp"]: g for g in f["glyphs"]}
        self.kern = {} if self.pix else {(a, b): round(k) for a, b, k in kern[str(self.weight)]}


def shape(s, pix):
    out, i = [], 0
    while i < len(s):
        if not pix and s[i] == "f" and i + 1 < len(s) and s[i + 1] in "il":
            out.append(0xE000 if s[i + 1] == "i" else 0xE001)
            i += 2
        else:
            out.append(ord(s[i]))
            i += 1
    return out


def adv_fx(adv_units, size):
    """Avanzamento in 16.16 come lo passa Chrome a HarfBuzz: troncato (SkiaScalarToHarfBuzzPosition)."""
    return adv_units * size * 65536 // 1000


def kern_fx(k_units, size):
    """Kerning GPOS scalato da HarfBuzz: em_mult, arrotondato (v * mult + 32768) >> 16."""
    mult = (size * 65536 << 16) // 1000
    return (k_units * mult + 32768) >> 16


def py_render(font, s, x, y, align, inv):
    """Stesso algoritmo di text.cpp: posizioni in 16.16 come HarfBuzz, fase a 1/4 di pixel come Skia."""
    cps = [c for c in shape(s, font.pix) if c in font.g]
    us, u, prev = [], 0, None
    for cp in cps:
        if prev is not None:
            u += kern_fx(font.kern.get((prev, cp), 0), font.size)
        us.append(u)
        u += adv_fx(units(font.g[cp]["adv"], font.size), font.size)
        prev = cp
    a = {"left": 0, "center": 1, "right": 2}[align]
    px = set()
    for cp, ui in zip(cps, us):
        p2 = 2 * x * 65536 - a * u + 2 * ui  # posizione in 1/131072 px
        q = (4 * p2 + 65536) // 131072
        b = (font.g[cp]["inv"] if inv else font.g[cp]["phases"])[q & 3]
        bits = unhex(b["bits"], b["w"] * b["h"])
        for yy in range(b["h"]):
            for xx in range(b["w"]):
                if bits[yy * b["w"] + xx]:
                    px.add(((q >> 2) + b["x"] + xx, y + b["y"] + yy))
    return px, u / 65536


def evaluate(data, inv_combos):
    fonts = {(f["size"], f["weight"], f["pix"]): PyFont(f, data["kern"]) for f in data["fonts"]}
    rows = []
    for r in data["refs"]:
        w = (400 if r["weight"] <= 500 else 700) if r["pix"] else r["weight"]  # matching CSS dei pesi
        key = (r["size"], w, r["pix"])
        font = fonts[key]
        inv = r["paper"] and key in inv_combos
        lines = r["lines"][: r["wrap"]["maxLines"]] if r["wrap"] else [r["fitted"]]
        px = set()
        for i, ln in enumerate(lines):
            y = round(r["y"]) + (i * r["wrap"]["lh"] if r["wrap"] else 0)
            p, _ = py_render(font, ln, round(r["x"]), y, r["align"], inv)
            px |= p
        px = {(a, b) for a, b in px if 0 <= a < W and 0 <= b < H}
        ref = unhex(r["bits"], W * H)
        refset = {(i % W, i // W) for i, v in enumerate(ref) if v}
        if r["bg"]:
            bx, by, bw, bh = r["bg"]
            bgset = {(a, b) for a in range(bx, bx + bw) for b in range(by, by + bh) if 0 <= a < W and 0 <= b < H}
            px = bgset - px
            ink = len(bgset - refset)
        else:
            ink = len(refset)
        rows.append(dict(name=r["name"], diff=len(px ^ refset), ink=ink))
    return rows


# ---------------------------------------------------------------- emissione C++
def cname(size, weight, pix):
    return f"{'pix' if pix else 'ui'}{weight}_{size}"


def cp_comment(cp):
    if cp == 0xE000:
        return "legatura fi"
    if cp == 0xE001:
        return "legatura fl"
    ch = chr(cp)
    return {" ": "spazio", "\\": "barra rovescia"}.get(ch, ch)


def emit_font(f, inv_on, charset, phases):
    size, weight, pix = f["size"], f["weight"], f["pix"]
    name = cname(size, weight, pix)
    glyphs = sorted((g for g in f["glyphs"] if g["cp"] in charset), key=lambda g: g["cp"])
    bitmap, dedup, recs, entries = bytearray(), {}, [], []
    for g in glyphs:
        first = len(recs)
        for b in g["phases"] + (g["inv"] if inv_on else []):
            data = pack(unhex(b["bits"], b["w"] * b["h"]))
            key = (b["w"], b["h"], data)
            if key not in dedup:  # fasi o varianti identiche condividono i byte
                dedup[key] = len(bitmap)
                bitmap += data
            recs.append((dedup[key], b["w"], b["h"], b["x"], b["y"]))
        entries.append((g["cp"], units(g["adv"], size), first))
    assert len(bitmap) < 65536 and len(recs) < 65536, name
    for r in recs:
        assert 0 <= r[1] < 256 and 0 <= r[2] < 256 and -128 <= r[3] < 128 and -128 <= r[4] < 128, (name, r)
    L = ["// Generato da tools/fonts/gen.py: non modificare a mano.",
         f"// {'Silkscreen' if pix else 'Chakra Petch'} {weight} {size}px · {len(entries)} glifi · {phases} fasi"
         f"{' + varianti chiaro su scuro' if inv_on else ''}",
         "#pragma once", '#include "font_types.h"', "", "namespace fonts {", ""]
    L.append(f"static const uint8_t {name}_bitmap[] FONT_DATA = {{")
    for i in range(0, len(bitmap), 24):
        L.append("  " + ", ".join(f"0x{b:02x}" for b in bitmap[i:i + 24]) + ",")
    L.append("};")
    L.append(f"static const Glyph {name}_glyphs[] FONT_DATA = {{")
    for i in range(0, len(recs), 4):
        L.append("  " + " ".join(f"{{{o}, {w}, {h}, {x}, {y}}}," for o, w, h, x, y in recs[i:i + 4]))
    L.append("};")
    L.append(f"static const GlyphEntry {name}_entries[] FONT_DATA = {{")
    for cp, adv, first in entries:
        L.append(f"  {{0x{cp:04x}, {adv}, {first}}},  // {cp_comment(cp)}")
    L.append("};")
    L.append("")
    L.append("}  // namespace fonts")
    sizes = dict(bitmap=len(bitmap), glyphs=len(recs) * 6, entries=len(entries) * 6)
    return name, "\n".join(L) + "\n", len(entries), sizes


def emit_kern(kern):
    keysets = {w: sorted((a << 16) | b for a, b, _ in pairs) for w, pairs in kern.items()}
    ws = sorted(kern)
    shared = all(keysets[w] == keysets[ws[0]] for w in ws)
    L = ["// Generato da tools/fonts/gen.py: non modificare a mano.",
         "// Kerning (GPOS 'kern' di Chakra Petch) in millesimi di em, misurato in Chrome a 1000 px.",
         "// Chiave = (primo codepoint << 16) | secondo, ordinata per la ricerca binaria.",
         "#pragma once", '#include "font_types.h"', "", "namespace fonts {", ""]
    total = 0
    for w in ws:
        tag = "all" if shared else w
        if not shared or w == ws[0]:
            keys = keysets[w]
            L.append(f"static const uint32_t kern_keys_{tag}[] FONT_DATA = {{")
            for i in range(0, len(keys), 8):
                L.append("  " + ", ".join(f"0x{k:08x}" for k in keys[i:i + 8]) + ",")
            L.append("};")
            total += 4 * len(keys)
        vals = {(a << 16) | b: round(k) for a, b, k in kern[w]}
        vs = [vals[k] for k in keysets[w]]
        L.append(f"static const int16_t kern_vals_{w}[] FONT_DATA = {{")
        for i in range(0, len(vs), 16):
            L.append("  " + ", ".join(str(v) for v in vs[i:i + 16]) + ",")
        L.append("};")
        total += 2 * len(vs)
        L.append(f"static const KernTable kern_{w} = {{kern_keys_{tag}, kern_vals_{w}, {len(vs)}}};")
        L.append("")
    L.append("}  // namespace fonts")
    return "\n".join(L) + "\n", total, len(keysets[ws[0]])


def c_string(s):
    out = '"'
    for b in s.encode("utf-8"):
        ch = chr(b)
        if b < 0x20 or b >= 0x7F or ch in '"\\?':
            out += f'\\x{b:02x}""'
        else:
            out += ch
    return out + '"'


def emit_fidelity(refs, path):
    L = ["// Generato da tools/fonts/gen.py: non modificare a mano.",
         "// Stringhe reali del simulatore disegnate in Chrome con T()/wrapT() su 250×122 e portate a 1 bit",
         "// come Screen.compose (luminanza < 0.5 = inchiostro).", "#pragma once", "#include <stdint.h>", "",
         "struct FidelityRef {", "  const char *name, *s, *fitted;", "  int size, weight;", "  bool pix;", "  int align;",
         "  int x, y, max;", "  bool paper;", "  int bg[4];", "  int wrapW, wrapLines, wrapLh;",
         "  int32_t width1000, fitted1000;  // measureText(s) e measureText(fitted) in millesimi di px",
         "  int nLines;", "  const char *lines[4];  // wrapLines() del simulatore",
         "  const uint8_t *bits;  // 250×122, MSB first, riga per riga", "};", ""]
    for i, r in enumerate(refs):
        data = pack(unhex(r["bits"], W * H))
        L.append(f"static const uint8_t fid_bits_{i}[] = {{")
        for j in range(0, len(data), 32):
            L.append("  " + ",".join(f"0x{b:02x}" for b in data[j:j + 32]) + ",")
        L.append("};")
    L.append(f"static const int kFidelityCount = {len(refs)};")
    L.append("static const FidelityRef kFidelity[] = {")
    for i, r in enumerate(refs):
        a = {"left": 0, "center": 1, "right": 2}[r["align"]]
        bg = r["bg"] or [0, 0, 0, 0]
        wr = r["wrap"] or {"maxW": 0, "maxLines": 0, "lh": 0}
        lines = (r["lines"] or [])
        lines_c = ", ".join(c_string(x) for x in lines[:4]) or "nullptr"
        L.append(f"  {{{c_string(r['name'])}, {c_string(r['s'])}, {c_string(r['fitted'])}, {r['size']}, {r['weight']}, "
                 f"{'true' if r['pix'] else 'false'}, {a}, {round(r['x'])}, {round(r['y'])}, {r['max']}, "
                 f"{'true' if r['paper'] else 'false'}, {{{', '.join(map(str, bg))}}}, "
                 f"{wr['maxW']}, {wr['maxLines']}, {wr['lh']}, "
                 f"{round(r['width'] * 1000)}, {round(r['fittedWidth'] * 1000)}, {len(lines)}, {{{lines_c}}}, fid_bits_{i}}},")
    L.append("};")
    path.write_text("\n".join(L) + "\n")


def evaluate_wide(data, inv_combos):
    fonts = {(f["size"], f["weight"], f["pix"]): PyFont(f, data["kern"]) for f in data["fonts"]}
    tot_d = tot_i = 0
    worst_w, worst = 0.0, []
    for r in data.get("wide", []):
        font = fonts[(r["size"], r["weight"], r["pix"])]
        px, w = py_render(font, r["s"], 4, r["size"] // 2, "left", False)
        ref = unhex(r["bits"], r["ww"] * r["hh"])
        refset = {(i % r["ww"], i // r["ww"]) for i, v in enumerate(ref) if v}
        px = {(a, b) for a, b in px if 0 <= a < r["ww"] and 0 <= b < r["hh"]}
        d = len(px ^ refset)
        tot_d += d
        tot_i += len(refset)
        worst_w = max(worst_w, abs(w - r["width"]))
        worst.append((d / max(1, len(refset)), d, r["s"], r["size"], r["weight"], r["pix"]))
    worst.sort(reverse=True)
    return dict(n=len(data.get("wide", [])), diff=tot_d, ink=tot_i, max_width_err_px=worst_w,
                worst=[w[1:] for w in worst[:8]], strings_with_diff=sum(1 for w in worst if w[1]))


def run(data, cfg):
    out_fonts, out_test, cache = cfg["out_fonts"], cfg["out_test"], cfg["cache"]
    inv_combos, big_min, big_chars = cfg["inv_combos"], cfg["big_min"], cfg["big_chars"]
    all_cps = {g["cp"] for g in data["fonts"][0]["glyphs"] if g["cp"] < 0xE000} | {0xE000, 0xE001}
    out_fonts.mkdir(parents=True, exist_ok=True)
    for old in out_fonts.glob("f_*.h"):
        old.unlink()
    reg, summary = [], dict(fonts=[], browser=data["browser"])
    tot = dict(bitmap=0, glyphs=0, entries=0)
    for f in data["fonts"]:
        key = (f["size"], f["weight"], f["pix"])
        charset = all_cps if f["size"] < big_min else {ord(c) for c in big_chars} | {0xE000, 0xE001}
        inv_on = key in inv_combos
        name, text, n, sizes = emit_font(f, inv_on, charset, cfg["phases"])
        (out_fonts / f"f_{name}.h").write_text(text)
        reg.append((key, name, n, inv_on))
        for k in tot:
            tot[k] += sizes[k]
        summary["fonts"].append(dict(name=name, n=n, inv=inv_on, **sizes))
    ktext, kbytes, npairs = emit_kern(data["kern"])
    (out_fonts / "f_kern.h").write_text(ktext)
    L = ["// Generato da tools/fonts/gen.py: non modificare a mano.",
         "// Registro dei font: (size, weight, pix) -> dati. Ordinato per pix, size, weight.",
         "#pragma once", '#include "font_types.h"', '#include "f_kern.h"']
    reg.sort(key=lambda r: (r[0][2], r[0][0], r[0][1]))
    L += [f'#include "f_{name}.h"' for _, name, _, _ in reg]
    L += ["", "namespace fonts {", "", "static const Font kFonts[] = {"]
    for (size, weight, pix), name, n, inv_on in reg:
        kern = "nullptr" if pix else f"&kern_{weight}"
        L.append(f"  {{{size}, {weight}, {'true' if pix else 'false'}, {'true' if inv_on else 'false'}, {n}, "
                 f"{name}_entries, {name}_glyphs, {name}_bitmap, {kern}}},")
    L += ["};", "static const int kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);", "", "}  // namespace fonts", ""]
    (out_fonts / "f_registry.h").write_text("\n".join(L))
    emit_fidelity(data["refs"], out_test)

    rows = evaluate(data, inv_combos)
    summary.update(bytes=dict(tot, kern=kbytes, total=sum(tot.values()) + kbytes), kern_pairs=npairs,
                   python_fidelity=rows, fallback=cfg["fallback"])
    (cache / "summary.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False))
    print(f"font: {len(reg)} · byte: bitmap {tot['bitmap']} + glifi {tot['glyphs']} + indici {tot['entries']} "
          f"+ kerning {kbytes} = {summary['bytes']['total']}")
    d = sum(r["diff"] for r in rows)
    i = sum(r["ink"] for r in rows)
    print(f"fedeltà (emulazione Python): {d} pixel diversi su {i} pixel di testo ({100 * d / i:.2f}%)")
    for r in rows:
        if r["diff"]:
            print(f"  {r['name']}: {r['diff']} px su {r['ink']}")
    wide = evaluate_wide(data, inv_combos)
    summary["python_wide"] = wide
    (cache / "summary.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False))
    if wide["n"]:
        print(f"controllo largo: {wide['n']} stringhe, {wide['diff']} pixel diversi su {wide['ink']} "
              f"({100 * wide['diff'] / max(1, wide['ink']):.3f}%), {wide['strings_with_diff']} stringhe con differenze, "
              f"errore di larghezza max {wide['max_width_err_px']:.4f} px")
        for w in wide["worst"][:5]:
            print("   ", w)
    print("fallback:", cfg["fallback"])
