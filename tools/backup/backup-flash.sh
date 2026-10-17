#!/usr/bin/env bash
# Backup completo della flash (8 MB sulla Heltec Wireless Paper). Solo lettura: non cambia niente sulla scheda.
#   tools/backup/backup-flash.sh [PORTA]
# Esce in backup-fabbrica/<chip>_<mac>_<data>.bin con accanto il file .sha256.
# DIMENSIONE (default 0x800000 = 8 MB) e BAUD (default 115200) si cambiano dall'ambiente.
set -euo pipefail
source "$(dirname "$0")/comune.sh"

PORTA="$(trova_porta "${1:-}")"
DIMENSIONE="${DIMENSIONE:-0x800000}"
echo "Scheda sulla porta $PORTA, leggo chip e MAC…"
leggi_scheda

CARTELLA="$RADICE/backup-fabbrica"
mkdir -p "$CARTELLA"
FILE="$CARTELLA/${CHIP}_${MAC}_$(date +%Y%m%d-%H%M%S).bin"
echo "Leggo $DIMENSIONE byte in $FILE (a $BAUD baud 8 MB richiedono circa 12 minuti)…"
"${ESPTOOL[@]}" --port "$PORTA" --baud "$BAUD" read-flash 0 "$DIMENSIONE" "$FILE"

# impronta nel formato di «shasum -a 256 -c»
(cd "$CARTELLA" && shasum -a 256 "$(basename "$FILE")" > "$(basename "$FILE").sha256")
echo "Fatto: $FILE"
echo "Impronta: $(cut -d' ' -f1 "$FILE.sha256")"
echo "Il backup contiene la password del Wi-Fi e le chiavi della scheda: tienilo privato, mai nel repository."
