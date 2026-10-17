#!/usr/bin/env bash
# Rimette sulla scheda un backup completo fatto con backup-flash.sh (o con lo strumento ROM).
#   tools/backup/restore-flash.sh FILE.bin [PORTA]
# DISTRUTTIVO: sostituisce tutta la flash. Prima di scrivere chiede di digitare SCRIVI.
set -euo pipefail
source "$(dirname "$0")/comune.sh"

FILE="${1:-}"
if [ -z "$FILE" ] || [ ! -f "$FILE" ]; then
  echo "Uso: $0 FILE.bin [PORTA]" >&2
  exit 1
fi

# impronta: se c'è il .sha256 accanto, deve tornare
if [ -f "$FILE.sha256" ]; then
  (cd "$(dirname "$FILE")" && shasum -a 256 -c "$(basename "$FILE").sha256") || { echo "Il file non corrisponde alla sua impronta: non scrivo." >&2; exit 1; }
else
  echo "Attenzione: manca $FILE.sha256, non posso controllare che il file sia integro."
fi

PORTA="$(trova_porta "${2:-}")"
echo "Scheda sulla porta $PORTA, leggo chip e MAC…"
leggi_scheda

echo
echo "  File:      $FILE ($(stat -f%z "$FILE") byte)"
echo "  Scheda:    $CHIP, MAC $MAC, porta $PORTA"
echo "  Indirizzo: 0x0 (tutta la flash)"
# un backup di un'altra scheda non funziona: Wi-Fi e chiavi sono cifrati con la chiave del chip (eFuse)
case "$(basename "$FILE")" in
  *"_${MAC}_"*) ;;
  *)
    echo
    echo "Il nome del file non contiene il MAC di questa scheda ($MAC): forse viene da un'altra."
    read -r -p "Per scrivere lo stesso digita FORZA: " r
    [ "$r" = "FORZA" ] || { echo "Annullato."; exit 1; } ;;
esac
echo
echo "La scrittura sostituisce tutto quello che c'è sulla scheda e non si annulla."
read -r -p "Per confermare digita SCRIVI: " r
[ "$r" = "SCRIVI" ] || { echo "Annullato: niente è stato scritto."; exit 1; }

# write-flash controlla da solo l'hash di ogni pezzo scritto
"${ESPTOOL[@]}" --port "$PORTA" --baud "$BAUD" write-flash 0x0 "$FILE"
echo "Fatto. Scollega e ricollega la scheda per riavviarla."
