#!/usr/bin/env python3
"""Portale Wi-Fi di Calamaio OS: dai sorgenti alla scheda e all'anteprima.

    python3 firmware/portal/build.py            # rigenera src/net/portal_assets.h e preview/portal.html
    python3 firmware/portal/build.py --subset   # in più rifà i font ridotti (serve fontTools + brotli)

Sorgenti:
  portal/src/portal.html   pagina unica (scelta rete, password, attesa, esito, stato)
  mascot() e HW            copiati AL MOMENTO dal simulatore (reference/…/calamaio-test.js), senza modifiche
  portal/fonts/*.woff2     Chakra Petch 600/700 e Silkscreen 400 ridotti ai caratteri latini (--subset)

Uscite:
  src/net/portal_assets.h  pagina gzip + font, serviti dalla scheda (flash)
  portal/preview/portal.html  la stessa pagina con i font incorporati e dati finti: si apre con doppio clic
                              sul Mac (?modo=sta per la pagina di stato; password «sbagliata» = esito errore)
Solo libreria standard, tranne --subset.
"""
import base64
import gzip
import io
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FW = HERE.parent
ROOT = FW.parent
SIM = ROOT / 'reference/calamaio-os-browser/assets/calamaio-test.js'
REF_FONTS = ROOT / 'reference/calamaio-os-browser/assets/fonts'

# (file ridotto, sorgente del simulatore, famiglia, peso, url sulla scheda)
FONTS = [
    ('cp600.woff2', 'chakra-petch-latin-600-normal.woff2', 'Chakra Petch', 600, '/f/cp600.woff2'),
    ('cp700.woff2', 'chakra-petch-latin-700-normal.woff2', 'Chakra Petch', 700, '/f/cp700.woff2'),
    ('sk400.woff2', 'silkscreen-latin-400-normal.woff2', 'Silkscreen', 400, '/f/sk400.woff2'),
]
# ASCII stampabile, Latin-1 (accenti italiani e non solo: anche i nomi delle reti) e la punteggiatura dei testi
UNICODES = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2039, 0x203A, 0x20AC, 0x2212]


def subset_fonts():
    from fontTools import subset  # pip install fonttools brotli
    (HERE / 'fonts').mkdir(exist_ok=True)
    for name, src, *_ in FONTS:
        opts = subset.Options()
        opts.flavor = 'woff2'
        opts.layout_features = ['kern', 'liga']
        opts.name_IDs = ['*']  # tiene copyright e licenza (SIL OFL) nel font
        font = subset.load_font(str(REF_FONTS / src), opts)
        s = subset.Subsetter(opts)
        s.populate(unicodes=UNICODES)
        s.subset(font)
        subset.save_font(font, str(HERE / 'fonts' / name), opts)
        print(f'  {name}: {(REF_FONTS / src).stat().st_size} → {(HERE / "fonts" / name).stat().st_size} byte')


def mascot_source():
    """HW e function mascot(...) del simulatore, testo identico (come tools/golden/mascot-golden.js)."""
    js = SIM.read_text(encoding='utf-8')
    hw = re.search(r'const HW = \[[^\]]*\];', js).group(0)
    start = js.index('function mascot(')
    i = js.index('{', start)
    depth = 0
    while True:
        if js[i] == '{':
            depth += 1
        elif js[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    return hw + '\n' + js[start:i + 1]


def font_css(inline):
    rules = []
    for name, _src, fam, w, url in FONTS:
        if inline:
            data = base64.b64encode((HERE / 'fonts' / name).read_bytes()).decode()
            url = 'data:font/woff2;base64,' + data
        rules.append(f"@font-face{{font-family:'{fam}';font-weight:{w};font-style:normal;font-display:swap;"
                     f"src:url({url}) format('woff2')}}")
    return '\n'.join(rules)


def page(inline_fonts):
    html = (HERE / 'src/portal.html').read_text(encoding='utf-8')
    html = html.replace('/*@@MASCOT@@*/', mascot_source())
    html = html.replace('/*@@FONTS@@*/', font_css(inline_fonts))
    return html


def c_array(name, data):
    out = io.StringIO()
    out.write(f'static const uint8_t {name}[{len(data)}] = {{\n')
    for k in range(0, len(data), 20):
        out.write('  ' + ','.join(str(b) for b in data[k:k + 20]) + ',\n')
    out.write('};\n')
    return out.getvalue()


def main():
    if '--subset' in sys.argv:
        subset_fonts()
    board_html = page(inline_fonts=False).encode('utf-8')
    gz = gzip.compress(board_html, compresslevel=9, mtime=0)
    fonts = [(name, url, (HERE / 'fonts' / name).read_bytes()) for name, _s, _f, _w, url in FONTS]

    h = io.StringIO()
    h.write('// Generato da firmware/portal/build.py: NON modificare a mano (sorgente: portal/src/portal.html).\n')
    h.write(f'// Pagina {len(board_html)} byte, gzip {len(gz)} byte; font ' +
            ', '.join(f'{n} {len(d)}' for n, _u, d in fonts) +
            f'; totale in flash {len(gz) + sum(len(d) for *_x, d in fonts)} byte.\n')
    h.write('#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\nnamespace portal {\n\n')
    h.write(c_array('kPage', gz))
    for k, (name, _u, data) in enumerate(fonts):
        h.write(c_array(f'kFont{k}', data))
    h.write('\nstruct Asset { const char *path; const char *type; const uint8_t *data; size_t len; bool gzip; };\n')
    h.write('static const Asset kPageAsset = {"/", "text/html; charset=utf-8", kPage, sizeof kPage, true};\n')
    h.write('static const Asset kFonts[] = {\n')
    for k, (_n, url, _d) in enumerate(fonts):
        h.write(f'  {{"{url}", "font/woff2", kFont{k}, sizeof kFont{k}, false}},\n')
    h.write('};\n\n}  // namespace portal\n')
    (FW / 'src/net/portal_assets.h').write_text(h.getvalue(), encoding='utf-8')

    (HERE / 'preview').mkdir(exist_ok=True)
    (HERE / 'preview/portal.html').write_text(page(inline_fonts=True), encoding='utf-8')
    total_served = len(gz) + sum(len(d) for *_x, d in fonts)
    print(f'pagina {len(board_html)} byte (gzip {len(gz)}), font {sum(len(d) for *_x, d in fonts)} byte, '
          f'primo caricamento {total_served} byte')
    print('scritti src/net/portal_assets.h e portal/preview/portal.html')


if __name__ == '__main__':
    main()
