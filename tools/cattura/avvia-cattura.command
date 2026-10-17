#!/bin/zsh
# Avvia il servizio Calamaio in modalità cattura (M0). Doppio clic su macOS.
# I payload finiscono in protocol/samples/capture-AAAA-MM-GG.jsonl
cd "$(dirname "$0")/../.." || exit 1
REPO="$PWD"
echo "Servizio Calamaio in cattura. Per fermarlo: Ctrl+C"
echo
echo "In un ALTRO terminale apri una sessione di prova con:"
echo "  claude --plugin-dir \"$REPO/plugin\" --settings \"$REPO/tools/cattura/settings-cattura.json\""
echo
exec calamaio serve --capture --samples "$REPO/protocol/samples"
