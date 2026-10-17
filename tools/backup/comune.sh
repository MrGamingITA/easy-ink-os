# Parti comuni di backup-flash.sh e restore-flash.sh (si include con «source», non si esegue).
# Non scrive mai le eFuse: usa solo read-mac, read-flash e write-flash di esptool.

QUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RADICE="$(cd "$QUI/../.." && pwd)"
BAUD="${BAUD:-115200}"   # con il CP2102 della Heltec 460800 e 921600 danno errori

# esptool: quello della .venv dello strumento ROM se c'è, altrimenti uvx (scarica esptool 5.x la prima volta)
if [ -x "$RADICE/tools/rom-tool/.venv/bin/esptool" ]; then
  ESPTOOL=("$RADICE/tools/rom-tool/.venv/bin/esptool")
elif command -v uvx >/dev/null 2>&1; then
  ESPTOOL=(uvx --from "esptool>=5.5,<6" esptool)
else
  echo "Manca esptool. Installa uv (brew install uv) oppure avvia una volta tools/rom-tool/avvia.command." >&2
  exit 1
fi

# porta: quella data, altrimenti l'unica porta USB-seriale collegata
trova_porta() {
  if [ -n "${1:-}" ]; then echo "$1"; return; fi
  local porte=()
  for p in /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/cu.usbmodem*; do
    [ -e "$p" ] && porte+=("$p")
  done
  if [ "${#porte[@]}" -eq 1 ]; then echo "${porte[0]}"; return; fi
  if [ "${#porte[@]}" -eq 0 ]; then
    echo "Nessuna scheda trovata. Collega la scheda con un cavo dati USB e riprova." >&2
  else
    echo "Ci sono più porte: ${porte[*]}. Scegline una e passala come argomento." >&2
  fi
  exit 1
}

# chip e MAC della scheda collegata (in CHIP e MAC, solo lettere e cifre)
leggi_scheda() {
  local out
  out="$("${ESPTOOL[@]}" --port "$PORTA" --baud "$BAUD" read-mac 2>&1)" || { echo "$out" >&2; echo "La scheda non risponde. Tieni premuto PRG mentre colleghi il cavo e riprova." >&2; exit 1; }
  CHIP="$(echo "$out" | grep -Eo 'ESP32(-[A-Z0-9]+)?' | head -n1 | tr 'A-Z' 'a-z' | tr -d '-')"
  MAC="$(echo "$out" | grep -Eio '([0-9a-f]{2}:){5}[0-9a-f]{2}' | head -n1 | tr 'A-F' 'a-f' | tr -d ':')"
  CHIP="${CHIP:-esp}"; MAC="${MAC:-nomac}"
}
