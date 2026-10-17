#!/bin/bash
# Doppio clic su macOS: crea il venv locale, installa i requisiti e apre la GUI.
cd "$(dirname "$0")" || exit 1
export PATH="$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin:$PATH"

pausa() { echo; read -r -n1 -p "Premi un tasto per chiudere questa finestra..."; echo; }
fallito() { echo; echo "ERRORE: $1"; pausa; exit 1; }

# Python con Tkinter: prima quello di mise, poi quello di sistema
BASE_PY="$HOME/.local/share/mise/installs/python/3.13.14/bin/python3"
[ -x "$BASE_PY" ] || BASE_PY="$(command -v python3)"
[ -n "$BASE_PY" ] || fallito "Python 3 non trovato."

if [ ! -x .venv/bin/python ]; then
  echo "Creo l'ambiente Python locale (.venv)..."
  if command -v uv >/dev/null 2>&1; then
    uv venv --seed --python "$BASE_PY" .venv || fallito "creazione del venv non riuscita."
  else
    "$BASE_PY" -m venv .venv || fallito "creazione del venv non riuscita."
  fi
fi

# reinstalla solo se requirements.txt è cambiato
STAMP=.venv/.requisiti.sha256
NUOVO="$(shasum -a 256 requirements.txt | cut -d' ' -f1)"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "$NUOVO" ]; then
  echo "Installo i requisiti (esptool, pyserial)..."
  if command -v uv >/dev/null 2>&1; then
    uv pip install --python .venv/bin/python -r requirements.txt || fallito "installazione non riuscita."
  else
    .venv/bin/python -m pip install -r requirements.txt || fallito "installazione non riuscita."
  fi
  echo "$NUOVO" > "$STAMP"
fi

if ! .venv/bin/python -c "import tkinter" >/dev/null 2>&1; then
  echo "Tkinter non è disponibile in questo Python: usa la riga di comando, ad esempio"
  echo "  .venv/bin/python rom_tool.py info"
  pausa
  exit 1
fi

# ROM_TOOL_AUTOCHIUDI=secondi serve solo ai test (la finestra si chiude da sola)
if [ -n "$ROM_TOOL_AUTOCHIUDI" ]; then
  exec .venv/bin/python rom_tool.py gui --autochiudi "$ROM_TOOL_AUTOCHIUDI"
fi
.venv/bin/python rom_tool.py gui || fallito "la finestra si è chiusa con un errore (vedi sopra)."
