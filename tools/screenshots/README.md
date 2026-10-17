# screenshots

Immagini del README in `docs/img/`, tutte generate (niente disegnato a mano, solo dati finti di `demo-state.json`):

- `display-*.png`: `device.cpp` compilato con i sorgenti del firmware (`src/core`, `src/gfx`, `src/ui`) disegna le schermate 250×122 con `ui::compose`, il tema `protocol/themes/fixtures/prova.cthm` e l'emulatore Game Boy con la ROM di prova `firmware/test/roms/dmg-acid2.gb` (MIT); poi ×3 pixel per pixel in una cornice semplice;
- `web-*.png`: la webapp servita da `tools/webui-dev/mock-server.mjs`, fotografata con Playwright a 1280 px.

Rigenerare (serve `c++`, le librerie native di PlatformIO e Playwright in `tools/webui-dev`):

```sh
(cd firmware && pio test -e native -f test_core)   # una volta: scarica ArduinoJson in .pio/libdeps/native
(cd tools/webui-dev && npm install)                 # una volta: Playwright
node tools/screenshots/make.mjs                     # tutto; --only device | --only web
```
