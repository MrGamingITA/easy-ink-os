---
name: setup
description: Guida l'utente a installare il servizio Calamaio sul PC, collegare la status line di Claude Code e caricare questo plugin, così le sessioni compaiono sul display e-ink. Usare quando l'utente chiede di installare, configurare, verificare o disinstallare Calamaio.
---

# Configurare Calamaio

Segui i passi in ordine e fermati al primo che non va. Non modificare mai `~/.claude/settings.json` a mano: lo fa `calamaio setup claude`, che chiede conferma e fa un backup.

1. **Binario.** Esegui `calamaio version` (deve rispondere `0.2.0-m4` o più nuova).
   - Se manca: dal repository, con Go installato, `cd service && go build -o ~/.local/bin/calamaio ./cmd/calamaio`. Su Windows: `go build -o %LOCALAPPDATA%\calamaio\calamaio.exe ./cmd/calamaio` e aggiungi la cartella al PATH.
   - `calamaio` deve essere nel PATH che vede Claude Code (`command -v calamaio`). Se `~/.local/bin` non c'è, spiega come aggiungerlo al profilo della shell e fermati.

2. **Servizio.** `curl -s http://127.0.0.1:8765/debug` deve rispondere.
   - Se non risponde, proponi di avviarlo in un terminale a parte: `calamaio serve` (Ctrl+C per fermarlo). L'avvio automatico al login non c'è ancora.
   - `calamaio serve --fake` mostra sessioni finte: serve solo per provare il display, non per l'uso vero.

3. **Status line.** Un plugin non la può impostare. Spiega all'utente che `calamaio setup claude`:
   - modifica `~/.claude/settings.json` (o `$CLAUDE_CONFIG_DIR/settings.json`) solo dopo una conferma `y`;
   - salva prima una copia accanto al file (`settings.json.calamaio-AAAAMMGG-HHMMSS.bak`);
   - ricorda la status line precedente, che continua a comparire: Calamaio la esegue e ne stampa l'output;
   - si annulla con `calamaio setup claude --undo`.
   Chiedi il consenso esplicito, poi fai lanciare all'utente il comando nel suo terminale (la conferma è interattiva).

4. **Plugin.** Per ora si carica all'avvio: `claude --plugin-dir <repository>/plugin`. Gli hook chiamano `calamaio hook`: se il servizio è spento escono subito, in silenzio, e non rallentano Claude Code. Le modifiche al plugin valgono dalla sessione successiva (o con `/reload-plugins`).

5. **Abbinamento (per i permessi dal display).** Con il display acceso e collegato: `calamaio pair` (o «Abbina» nella pagina Dispositivo della webapp). Il display mostra un codice di 6 cifre: va scritto sul PC (una prova sola), poi confermato sul display con due pressioni. Poi si accende sul display Menu › Avvisi › Permessi dal display. `calamaio status` dice «abbinato e autenticato». Per togliere l'abbinamento: `calamaio unpair <display>`.

6. **Verifica.** Apri una sessione con il plugin, scrivi un prompt e poi esegui `calamaio status`: la sessione deve comparire (lavora, poi finito). Nella webapp `http://127.0.0.1:8765/` compare nella Panoramica.

Regole da ricordare all'utente:
- i permessi dal display non sono mai automatici: NEGA è sempre preselezionato e serve una conferma sul display; arrivano solo a un display abbinato e autenticato con «Permessi dal display» acceso;
- una richiesta di permesso non scade: resta aperta finché rispondi, sul display o nel terminale (vale la prima risposta);
- le domande di Claude (scelte multiple, piani da approvare) si rispondono solo nel terminale;
- «Archivia» toglie dal display una sessione finita, in pausa o in errore finché non riprende a lavorare.
