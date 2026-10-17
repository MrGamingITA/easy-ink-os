# /// script
# requires-python = ">=3.10"
# dependencies = ["playwright", "fonttools", "brotli"]
# ///
"""Genera i font bitmap del firmware rasterizzando con Chrome, come fa il simulatore.

Uso (dalla radice del repo):  uv run tools/fonts/gen.py
Opzioni: --cache (riusa tools/fonts/.cache/extract.json senza riaprire il browser)

Scrive:  firmware/src/gfx/fonts/*.h        (dati dei font, registro)
         firmware/test/test_text/fidelity.h  (riferimenti del test di fedeltà)
         tools/fonts/.cache/extract.json     (dati grezzi del browser)
         tools/fonts/.cache/summary.json     (fallback, dimensioni, numeri del confronto)
"""
import argparse
import base64
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "reference/calamaio-os-browser/assets"
FONT_DIR = ASSETS / "fonts"
SIM_JS = ASSETS / "calamaio-test.js"
OUT_FONTS = ROOT / "firmware/src/gfx/fonts"
OUT_TEST = ROOT / "firmware/test/test_text/fidelity.h"
CACHE = Path(__file__).resolve().parent / ".cache"

# Stessi font string del simulatore (calamaio-test.js, costanti F_UI e F_PIX).
F_UI = '"Chakra Petch", "Arial Narrow", sans-serif'
F_PIX = '"Silkscreen", monospace'

# Combinazioni (size, weight, pix) usate dalla classe S (Heltec 250×122). Vedi README per l'origine.
COMBOS = [
    *[(s, 700, False) for s in (10, 12, 13, 14, 15, 16, 18, 20, 22, 26, 40, 44, 46)],
    *[(s, 600, False) for s in (10, 11, 12, 13)],
    *[(s, 500, False) for s in (10, 11, 12)],
    (8, 400, True),
    (8, 700, True),
]
# Silkscreen ha solo le facce 400 e 700: il peso 600 (guest, 'QR DI ESEMPIO') sceglie la 700, come
# fa il matching CSS dei pesi. Lo replica normWeight() in text.cpp, quindi non serve un font a parte.

# Varianti «chiaro su scuro» (c: PAPER nel simulatore): Chrome rasterizza il testo bianco su nero con
# copertura diversa, quindi la soglia dà pixel diversi. Solo le combinazioni che la classe S usa così.
INV_COMBOS = {(10, 700, False), (12, 700, False), (13, 700, False), (14, 700, False), (16, 700, False),
              (22, 700, False), (46, 700, False), (10, 600, False), (11, 600, False), (8, 400, True), (8, 700, True)}

# Dimensioni grandi (>= BIG_MIN): nella classe S mostrano solo numeri, orari, «min» e «ppm».
BIG_MIN = 26
BIG_CHARS = " 0123456789:.,%+-/?…" + "abcdefghijklmnopqrstuvwxyz"

# Fasi orizzontali rasterizzate per glifo (posizionamento subpixel di Chrome, quantizzato a 1/4 px).
PHASES = [0.0, 0.25, 0.5, 0.75]

ACCENTS = "àèéìíòóùúÀÈÉÌÒÙ"
SYMBOLS = "°·›‹″′−–—…✓✗▲▼◀▶●←→↑↓€×’"
CHARS = [chr(c) for c in range(0x20, 0x7F)] + list(ACCENTS) + list(SYMBOLS)
# Legature 'liga' di Chakra Petch (attive di default nel canvas): codepoint privati.
LIGS = {"fi": 0xE000, "fl": 0xE001}

W, H = 250, 122

# Stringhe reali del simulatore per il test di fedeltà (coordinate prese dalle schermate S).
REFS = [
    dict(name="boot_title", s="CALAMAIO OS", size=18, weight=700, x=99, y=28, max=145),
    dict(name="goccia_label", s="PERMESSO", size=20, weight=700, x=120, y=18, max=124),
    dict(name="cmd_600", s="Bash: npm run test", size=12, weight=600, x=9, y=22),
    dict(name="cmd_tool", s="Bash", size=12, weight=700, x=9, y=22),
    dict(name="righe_name", s="api-server", size=13, weight=700, x=26, y=1, max=150),
    dict(name="offline_menu", s="3X MENU", size=8, weight=700, pix=True, x=92, y=100),
    dict(name="detail_wrap", s="Guida di installazione aggiornata e 4 link rotti corretti.", size=10, weight=500,
         x=88, y=41, wrap=dict(maxW=158, maxLines=2, lh=12)),
    dict(name="long_fit", s="Guida di installazione aggiornata e 4 link rotti corretti.", size=10, weight=500,
         x=4, y=50, max=242),
    dict(name="offline_last", s="Ultimo dato alle 14:32", size=10, weight=600, x=92, y=56, max=154),
    dict(name="liga_fi", s="Apri la finestra", size=13, weight=700, x=95, y=46, max=149),
    dict(name="liga_fino", s="fino alle 15:05", size=10, weight=600, x=95, y=80, max=151),
    dict(name="connecting", s="Connessione a CasaWiFi..", size=12, weight=700, align="center", x=125, y=85, max=230),
    dict(name="perm_ok", s="CONSENTI", size=14, weight=700, align="center", x=63, y=63),
    dict(name="perm_no_inv", s="NEGA", size=14, weight=700, align="center", x=187, y=63, paper=True, bg=[126, 58, 118, 26]),
    dict(name="perm_strip_inv", s="PERMESSO", size=12, weight=700, x=4, y=1, paper=True, bg=[0, 0, 250, 15]),
    dict(name="focus_big", s="25 min", size=40, weight=700, x=95, y=20, max=151),
    dict(name="sleep_clock", s="14:32", size=44, weight=700, x=8, y=8),
    dict(name="perm_hint", s="1X CAMBIA   2X CONFERMA   LUNGA ANNULLA", size=8, weight=400, pix=True, x=4, y=109, max=242),
    dict(name="oggi_label", s="ATTESA MEDIA", size=8, weight=700, pix=True, align="center", x=188, y=96, max=115),
    dict(name="detail_task", s="› Aggiorna README", size=10, weight=600, x=88, y=92, max=158),
    dict(name="batt_pct", s="72%", size=22, weight=700, x=65, y=11),
    dict(name="ambiente_temp", s="21,5 °C  ·  48% umidità", size=11, weight=600, x=95, y=70, max=149),
    dict(name="clock_right", s="14:32", size=8, weight=400, pix=True, align="right", x=244, y=4),
    dict(name="accenti", s="È più bello così: perché", size=12, weight=600, x=6, y=30),
    dict(name="state_text", s="Sta lavorando", size=10, weight=500, x=120, y=61, max=124),
    dict(name="name_fit", s="calamaio-firmware-esp32-progetto", size=13, weight=700, x=26, y=1, max=150),
    dict(name="batt_bold", s="Risparmio energetico: sì", size=10, weight=700, x=8, y=80, max=234),
    dict(name="fallback_syms", s="✓ ✗ ▲ ▼ ◀ ▶ ● ← → ↑ ↓", size=12, weight=600, x=6, y=40),
    dict(name="net_check", s="CasaWiFi  ✓", size=12, weight=600, x=6, y=18, max=130),
    dict(name="sleep_count", s="3 sessioni · 1 ti aspettano", size=11, weight=600, x=8, y=103, max=150),
    dict(name="scanning", s="Ricerca reti…", size=12, weight=700, align="center", x=125, y=72),
    dict(name="toast_inv", s="Silenziato per 1 ora", size=10, weight=700, align="center", x=125, y=95, paper=True,
         bg=[60, 91, 130, 18], max=122),
    # Testo chiaro su fondo nero: tutte le combinazioni che la classe S disegna con c: PAPER.
    dict(name="list_sel_inv", s="Wi-Fi", size=12, weight=700, x=6, y=36, max=130, paper=True, bg=[0, 34, 245, 17]),
    dict(name="list_chev_inv", s="›", size=12, weight=700, align="right", x=240, y=36, paper=True, bg=[0, 34, 245, 17]),
    dict(name="list_val_inv", s="CasaWiFi", size=10, weight=600, align="right", x=226, y=38, max=108, paper=True,
         bg=[0, 34, 245, 17]),
    dict(name="detail_name_inv", s="api-server", size=12, weight=700, x=13, y=1, max=170, paper=True, bg=[0, 0, 250, 15]),
    dict(name="detail_idx_inv", s="2/3", size=8, weight=400, pix=True, align="right", x=246, y=4, paper=True,
         bg=[0, 0, 250, 15]),
    dict(name="strip_title_inv", s="WI-FI", size=8, weight=700, pix=True, x=13, y=3, max=180, paper=True,
         bg=[0, 0, 250, 14]),
    dict(name="strip_clock_inv", s="14:32", size=8, weight=400, pix=True, align="right", x=246, y=3, paper=True,
         bg=[0, 0, 250, 14]),
    dict(name="righe_name_inv", s="api-server", size=13, weight=700, x=26, y=32, max=150, paper=True,
         bg=[0, 31, 250, 17]),
    dict(name="righe_label_inv", s="PERMESSO", size=10, weight=700, align="right", x=244, y=34, paper=True,
         bg=[0, 31, 250, 17]),
    dict(name="numeri_inv", s="2", size=46, weight=700, align="center", x=125, y=6, paper=True, bg=[86, 3, 77, 74]),
    dict(name="numeri_lab_inv", s="PER TE", size=8, weight=700, pix=True, align="center", x=125, y=62, max=75,
         paper=True, bg=[86, 3, 77, 74]),
    dict(name="una_label_inv", s="PERMESSO", size=22, weight=700, x=8, y=26, max=146, paper=True, bg=[4, 25, 152, 27]),
    dict(name="detail_label_inv", s="ATTENDE TE", size=16, weight=700, x=88, y=19, max=158, paper=True,
         bg=[86, 18, 162, 20]),
    dict(name="perm_name_inv", s="api-server", size=11, weight=600, align="right", x=246, y=2, max=140, paper=True,
         bg=[0, 0, 250, 15]),
    dict(name="riepilogo_inv", s="PC SPENTO ALLE 18:42", size=8, weight=700, pix=True, x=4, y=4, max=242, paper=True,
         bg=[0, 0, 250, 15]),
    # Combinazioni che il simulatore usa solo in inchiostro (verifica e, per la 600, alias sulla 700).
    dict(name="una_name_15", s="api-server", size=15, weight=700, x=8, y=5, max=150),
    dict(name="guest_pix600", s="QR DI ESEMPIO", size=8, weight=600, pix=True, x=93, y=110, max=153),
    dict(name="pix600_inv_alias", s="QR DI ESEMPIO", size=8, weight=600, pix=True, x=93, y=100, max=153, paper=True,
         bg=[90, 98, 160, 12]),
    dict(name="guest_wrap", s="Inquadra il codice con la fotocamera per collegarti senza scrivere la password.",
         size=11, weight=500, x=93, y=60, wrap=dict(maxW=153, maxLines=3, lh=14)),
]


def sim_strings():
    """Letterali tra apici del simulatore con almeno 3 lettere: materiale per il controllo largo."""
    src = SIM_JS.read_text()
    out = set()
    for m in re.finditer(r"'((?:[^'\\\n]|\\.){3,80})'", src):
        t = m.group(1).replace("\\'", "'")
        if sum(ch.isalpha() for ch in t) >= 3 and "px" not in t and all(ch in CHARS for ch in t):
            out.add(t)
    return sorted(out)


# Combinazioni del controllo largo: le più usate dalla classe S.
WIDE_COMBOS = [(10, 500, False), (10, 600, False), (12, 700, False), (13, 600, False), (11, 600, False),
               (8, 400, True), (8, 700, True), (16, 700, False)]


def sim_source():
    """Estrae fit/T/wrapLines/wrapT dal simulatore (sola lettura), come tools/golden."""
    src = SIM_JS.read_text()
    out = []
    for name in ("fit", "T", "wrapLines", "wrapT"):
        start = src.index("function " + name + "(")
        i, d = src.index("{", start), 0
        while True:
            if src[i] == "{":
                d += 1
            elif src[i] == "}":
                d -= 1
                if d == 0:
                    break
            i += 1
        out.append(src[start:i + 1])
    return "\n".join(out)


def font_faces():
    """@font-face di Chakra Petch e Silkscreen da fonts.css, con i woff2 in data URL e le stesse unicode-range."""
    css = (FONT_DIR / "fonts.css").read_text()
    faces = []
    for m in re.finditer(r"@font-face\{[^}]*\}", css):
        rule = m.group(0)
        if "Chakra Petch" not in rule and "Silkscreen" not in rule:
            continue
        fn = re.search(r"url\(\./([^)]+)\)", rule).group(1)
        b64 = base64.b64encode((FONT_DIR / fn).read_bytes()).decode()
        faces.append((rule, fn, rule.replace(f"url(./{fn})", f"url(data:font/woff2;base64,{b64})")))
    return faces


def parse_ranges(rule):
    rng = re.search(r"unicode-range:([^;}]+)", rule).group(1)
    out = []
    for part in rng.split(","):
        part = part.strip()[2:]
        a, _, b = part.partition("-")
        out.append((int(a, 16), int(b or a, 16)))
    return out


def coverage():
    """Per ogni (famiglia, peso): codepoint disegnati dal font primario (unicode-range E cmap)."""
    from fontTools.ttLib import TTFont
    cov = {}
    for rule, fn, _ in font_faces():
        famname = "pix" if "Silkscreen" in rule else "ui"
        weight = int(re.search(r"font-weight:(\d+)", rule).group(1))
        cmap = TTFont(FONT_DIR / fn).getBestCmap()
        rs = parse_ranges(rule)
        s = cov.setdefault((famname, weight), set())
        for cp in cmap:
            if any(a <= cp <= b for a, b in rs):
                s.add(cp)
    return cov


def extract():
    from playwright.sync_api import sync_playwright
    faces = font_faces()
    html = "<!doctype html><html><head><meta charset='utf-8'><style>" + "\n".join(f[2] for f in faces) + \
        "</style></head><body></body></html>"
    cov = coverage()
    glyphs = [dict(cp=ord(ch), txt=ch, lig=False) for ch in CHARS] + \
        [dict(cp=cp, txt=t, lig=True) for t, cp in LIGS.items()]
    kern_glyphs = [g for g in glyphs if g["lig"] or g["cp"] in cov[("ui", 600)]]
    cfg = dict(
        F_UI=F_UI, F_PIX=F_PIX, simSrc=sim_source(), W=W, H=H,
        combos=[dict(size=s, weight=w, pix=p) for s, w, p in COMBOS],
        glyphs=glyphs, phases=PHASES, invert=True,
        allText="".join(CHARS),
        kernWeights=[500, 600, 700], kernGlyphs=kern_glyphs, ligPairs=list(LIGS),
        wide=[dict(s=t, size=sz, weight=w, pix=p) for t in sim_strings() for (sz, w, p) in WIDE_COMBOS],
        refs=[dict({"align": "left", "pix": False, "max": 0, "paper": False, "bg": None, "wrap": None}, **r) for r in REFS],
    )
    js = (Path(__file__).resolve().parent / "extract.js").read_text()
    with sync_playwright() as p:
        b = p.chromium.launch(channel="chrome")  # Google Chrome installato, come chi apre il simulatore
        pg = b.new_page()
        pg.set_content(html)
        data = pg.evaluate(js, cfg)
        data["browser"] = b.version
        b.close()
    data["coverage"] = {f"{k[0]}{k[1]}": sorted(v) for k, v in cov.items()}
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", action="store_true")
    a = ap.parse_args()
    CACHE.mkdir(exist_ok=True)
    cf = CACHE / "extract.json"
    if a.cache and cf.exists():
        data = json.loads(cf.read_text())
    else:
        data = extract()
        cf.write_text(json.dumps(data))
    import emit
    cov = {k: set(v) for k, v in data["coverage"].items()}
    fallback = {
        "Chakra Petch": "".join(c for c in CHARS if ord(c) not in cov["ui600"]),
        "Silkscreen": "".join(c for c in CHARS if ord(c) not in cov["pix400"]),
    }
    emit.run(data, dict(out_fonts=OUT_FONTS, out_test=OUT_TEST, cache=CACHE, inv_combos=INV_COMBOS,
                        big_min=BIG_MIN, big_chars=BIG_CHARS, phases=len(PHASES), fallback=fallback))


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    main()
