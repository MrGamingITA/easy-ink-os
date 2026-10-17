# Calamaio OS

Display e-ink a batteria (ESP32) che mostra lo stato delle sessioni di Claude Code. Monorepo: `firmware/` (PlatformIO), `service/` (Go), `plugin/` (plugin di Claude Code), `protocol/`, `tools/`.

Prima di lavorare leggi:
- `docs/ISTRUZIONI-SVILUPPO.md`: brief completo, milestone, regole sui permessi (sezione 7, non negoziabili);
- `docs/DECISIONI.md`: scelte e deviazioni prese finora;
- `docs/PROTOCOLLO.md` e `docs/HARDWARE.md` quando tocchi rete o schede.

Regole rapide:
- l'interfaccia si traduce dal simulatore in `reference/calamaio-os-browser/` (sola lettura, salvo allineamenti annotati in DECISIONI);
- testi a schermo in italiano identici al simulatore; identificatori in inglese; commenti brevi in italiano;
- mai inventare pin o controller: `TODO(hw)` e chiedere;
- backup e conferma prima di flash, cancellazioni NVS, modifiche a `~/.claude/settings.json`.

Test: `cd service && go test ./...` · plugin: `claude plugin validate plugin`.
