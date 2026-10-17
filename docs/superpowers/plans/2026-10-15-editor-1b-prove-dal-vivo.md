# Editor 1b: prove dal vivo (firmware 0.13.0-ed)

Copione per il coordinatore, da seguire **dopo un sì esplicito dell'utente** (il flash è già autorizzato, il backup no:
si fa sempre prima). Raccoglie le prove del Task B8, dei report B5 e B7 e del registro del piano 1b. Ogni prova ha
precondizioni, passi, atteso e cosa fare se fallisce. L'ordine conta: le prove che toccano file e NVS della scheda
stanno dopo i backup, quelle distruttive in fondo. I numeri misurati vanno in `docs/DECISIONI.md`, sezione «Editor
delle schermate» (voci «Misure», «Memoria», «Download nel task di rete», «Limiti noti»).

## Situazione di partenza (2026-10-16)

- Scheda **calamaio-48e8** (Heltec Wireless Paper, ESP32-S3), IP 192.168.1.58, abbinata e autenticata. Firmware
  **0.12.5-gb in app0**, ROM Pokémon Red in `gbrom` (0x490000), salvataggi in LittleFS `/gb` (partizione `spiffs`
  0x690000, 0x160000). Tabella `firmware/partitions_gb.csv`: NVS 0x9000 (0x5000), otadata 0xe000 (0x2000), app0
  0x10000, app1 0x250000.
- Servizio in esecuzione: `~/.local/bin/calamaio serve --firmware ~/CalamaiOS/firmware/.pio/build/heltec213`
  (genitore 1, non un LaunchAgent), binario del 12 ottobre **senza i temi** (`GET /api/themes` → 404 «API
  sconosciuta»). Serve il servizio del ramo `editor` (passo 0.5).
- Cavo USB: serve per i backup (0.3), per le righe del registro durante la partita (l'anello del registro è prestato
  alla cartuccia: in partita le righe vanno solo in seriale) e per i ripristini. Avvisare l'utente prima di ogni passo col
  cavo, perché la ricolleghi.

Variabili usate sotto (zsh):

```bash
R=~/CalamaiOS-editor
SP=<scratchpad>/b8
B=calamaio-48e8
PORT=/dev/cu.usbserial-0001
BK=~/CalamaiOS/backup-fabbrica      # ignorata da git: contiene chiavi e password
ESP=(pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port $PORT --baud 115200)   # da lanciare in $R/firmware
CAL=$SP/calamaio-editor
P="$HOME/Library/Application Support/calamaio"   # cartella dati del servizio (config.DataDir() su macOS)
J=(-H 'Content-Type: application/json' -H 'Origin: http://127.0.0.1:8765')
add()  { curl -s -X POST "${J[@]}" --data-binary @$1 http://127.0.0.1:8765/api/themes; echo; }          # file → archivio
send() { curl -s -X POST "${J[@]}" -d "{\"id\":$1}" http://127.0.0.1:8765/api/displays/$B/theme; echo; } # send '"prova"' · send null
st()   { curl -s http://127.0.0.1:8765/api/status | python3 -c '
import json,sys; d=json.load(sys.stdin); x=[x for x in d["displays"] if x["board"]=="'$B'"][0]; t=x["themes"]; j=t["job"]
print("fw", x["fw"], "| authed", x["authed"], "| supported", t["supported"], "| have", [h["id"] for h in t["have"]],
      "| cur", t["cur"], "| job", j and (j["theme"], j["stage"], j.get("error")))'; }
```

Registro della scheda: `$CAL logs --board $B --follow --minutes 60` in un terminale a parte (contiene anche le righe
dall'avvio: l'anello è di ~16 KB). Registro del servizio: `$SP/serve.log` (righe `temi …`). Seriale:
`pio device monitor -b 115200 -p $PORT` (l'apertura può riavviare la scheda: innocuo).

## Fase 0: preparazione

### 0.1 Build e controlli
```bash
cd $R/service && go test -count=1 ./... && cd $R/firmware && pio test -e native && pio run -e heltec213 | tail -3 \
 && cd $R/tools/webui-dev && npm test && cd $R && node tools/golden/theme-golden.mjs --check && node tools/fonts/export-js.mjs --check
```
Atteso: tutto verde; `firmware/.pio/build/heltec213/firmware.bin` da 1 945 824 byte (con l'arena del Runtime, revisione
finale; limite 2 359 296).
Se fallisce: ci si ferma, niente flash.

### 0.2 Temi di prova
```bash
mkdir -p $SP && cd $SP && cat > gen.py <<'EOF'
import json
F=lambda s,w: {"family":"ui","size":s,"weight":w}
T=lambda i,n,s: {"format":"calamaio-theme","version":1,"id":i,"name":n,"screen":"250x122","screens":s}
A=lambda *e: {"variants":[{"when":"always","elements":list(e)}]}
out={
 'secondi': T('secondi','Orologio coi secondi',{"home":A(
   {"type":"clock","x":0,"y":30,"w":250,"format":"hms","font":F(44,700),"align":"center"},
   {"type":"text","x":0,"y":90,"w":250,"text":"{sessioni} sessioni","font":F(13,700),"align":"center"})}),
 'costo': T('costo','Costo di oggi',{"oggi":A(
   {"type":"text","x":0,"y":20,"w":250,"text":"COSTO DI OGGI","font":F(13,700),"align":"center"},
   {"type":"text","x":0,"y":50,"w":250,"text":"{oggi.costo}","font":F(44,700),"align":"center"})}),
 'lampeggio': T('lampeggio','Riposo che lampeggia',{"sleep":A(
   {"type":"clock","x":8,"y":8,"w":140,"font":F(44,700)},
   {"type":"text","x":8,"y":70,"w":140,"text":"pari","font":F(13,700),"visibleIf":[{"blink":"even"}]},
   {"type":"mascot","x":158,"y":30,"scale":3,"ref":"nobody","pose":"idle"})}),
 'grande': T('grande','Tema grande',{n:A(*[{"type":"text","x":0,"y":(i*8)%120,"w":250,
   "text":(f"{n} {i} "+"abcdefghij"*25)[:240],"font":F(10,500)} for i in range(15)])
   for n in ["home","detail","sleep","offline","riepilogo","oggi","batteria","stato","limiti","focus","ambiente","pet"]}),
}
for i in range(1,10):
  d=json.load(open('~/CalamaiOS-editor/protocol/themes/fixtures/prova.theme.json'))
  d['id'],d['name']=f't{i}',f'Prova {i}'; out[f't{i}']=d
for k,v in out.items(): json.dump(v,open(f'{k}.theme.json','w'),ensure_ascii=False)
EOF
python3 gen.py && ls $SP/*.theme.json
```
Controllati col compilatore del servizio il 2026-10-16: `secondi` 147 byte, `costo` 148, `lampeggio` 157, `grande`
47 628 byte (12 blocchi da 3 955), `t1`…`t9` 675 byte (come `prova`), nessun avviso.

### 0.3 Backup della scheda (cavo, prima di tutto: la partita di 0.4 scrive `/gb`)
Avvisare l'utente, cavo collegato, servizio libero di perdere la scheda per qualche minuto.
```bash
cd $R/firmware && D=$(date +%Y%m%d-%H%M)
$ESP read_flash 0x9000 0x7000 $BK/nvs-otadata-$D-pre-0.13.0.bin        # NVS + otadata
$ESP read_flash 0x690000 0x160000 $BK/littlefs-$D-pre-0.13.0.bin       # /gb (salvataggi), avatar, cache (~2 min)
chmod 600 $BK/*-$D-pre-0.13.0.bin; shasum -a 256 $BK/*-$D-pre-0.13.0.bin
```
Atteso: due file da 28 672 e 1 441 792 byte. Se `read_flash` non si collega: tenere premuto PRG, premere e lasciare
RST, lasciare PRG (caricatore seriale), riprovare; poi RST per tornare a Calamaio. Senza backup non si va avanti.

### 0.4 Misure di riferimento sulla 0.12.5-gb (prima del flash, dopo i backup)
Servono per i confronti di 7 e 12. Con il servizio di adesso (`~/.local/bin/calamaio logs --board $B --minutes 5`):
annotare l'ultima `[memoria] heap libero … (minimo …), stack rete libero …` (una al minuto) e, se c'è stata,
`[memoria] Bluetooth acceso: …`. Con il cavo e il monitor seriale aperto: una partita di Pokémon Red (Menu › Gioco ›
ROM, controller collegato), annotare `[gb] memoria per la partita: servono … liberi … blocco massimo …` (**anche il
blocco massimo**), `[gb] RAM cartuccia: 16384 byte dall'anello del registro, … dallo heap`, `[gb] … pronto: … heap
libero …`, `[memoria] partita avviata: …` e una riga `[gb] emulatore: … heap libero … (minimo …)`; poi Salva ed esci.
Se si salta: i confronti si fanno con le stime di DECISIONI («Memoria»).

### 0.5 Servizio del ramo editor
Prima, il backup **di tutta la cartella dati del PC** (`config.DataDir()`: su macOS `~/Library/Application
Support/calamaio`, oppure `$CALAMAIO_DATA_DIR` se il servizio lo ha): `config.json` con l'abbinamento, `game.json`,
`oggi.json`, `ota-key`, `backups/` (l'originale di fabbrica), `crash/`, `elf/`. Il servizio nuovo scrive nella
stessa cartella: senza copia, tornare al vecchio binario non sarebbe recuperabile se un formato cambia. Si salta solo
`logs/`. Va in `$BK` (ignorata da git: contiene chiavi), non nello scratchpad (temporaneo).
```bash
ps eww -o command= $(pgrep -f 'calamaio serve') | tr ' ' '\n' | grep CALAMAIO_DATA_DIR   # vuoto = cartella di serie
D2=$(date +%Y%m%d-%H%M)
mkdir $BK/pc-data-$D2-pre-editor && rsync -a --exclude logs "$P/" $BK/pc-data-$D2-pre-editor/ && chmod 700 $BK/pc-data-$D2-pre-editor
diff -rq --exclude logs "$P" $BK/pc-data-$D2-pre-editor && du -sh $BK/pc-data-$D2-pre-editor
```
Atteso: `diff` muto, ~130 MB (quasi tutto `elf/`). Senza questa copia non si cambia servizio.
```bash
cd $R/service && go build -o $CAL ./cmd/calamaio
OLD=$(pgrep -f 'calamaio serve --firmware ~/CalamaiOS/firmware'); kill $OLD
nohup $CAL serve --firmware $R/firmware/.pio/build/heltec213 > $SP/serve.log 2>&1 &
sleep 3; curl -s http://127.0.0.1:8765/api/themes; echo; st
```
Atteso: `{"themes":[],"dir":"…/calamaio/themes"}`; la scheda si ricollega da sola (il vecchio servizio manda `bye`: la
scheda mostra «PC spento», dorme e riprova a tempo; PRG la sveglia subito) e `st` dà `fw 0.12.5-gb | authed True |
supported False`.
Se fallisce (o per tornare al vecchio servizio in qualunque momento): `kill` del nuovo, rimettere la cartella dati
della copia, poi il vecchio servizio:
```bash
pkill -f "$CAL serve"; rsync -a -c --delete --exclude logs $BK/pc-data-$D2-pre-editor/ "$P/" && chmod 700 "$P"   # -c: confronta il contenuto
nohup ~/.local/bin/calamaio serve --firmware ~/CalamaiOS/firmware/.pio/build/heltec213 >/dev/null 2>&1 &
```

### 0.6 Flash della 0.13.0-ed (aggiornamento remoto)
```bash
$CAL ota push --board $B --build $R/firmware/.pio/build/heltec213
```
Va in app1; la 0.12.5-gb resta in app0. Atteso: fasi fino a `fatto`, riavvio, la nuova versione diventa valida dopo
Wi-Fi + autenticazione entro 120 s (OTA.md). Non avviare partite in quei 120 s.
Se fallisce: un'immagine che non si avvia torna da sola alla precedente («la nuova versione non si è avviata: tornato
alla precedente»). Ripiego col cavo, solo con un sì: rimettere l'otadata del backup (punta ad app0, la 0.12.5-gb):
`dd if=$BK/nvs-otadata-<D>-pre-0.13.0.bin of=$SP/otadata.bin bs=4096 skip=5 count=2 && $ESP write_flash 0xe000 $SP/otadata.bin`.
Strada via cavo alternativa per il flash (115200, solo otadata e app0):
`$ESP --before default_reset --after hard_reset write_flash 0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 $R/firmware/.pio/build/heltec213/firmware.bin`
(sovrascrive la 0.12.5-gb in app0: in quel caso il ripiego è rifare lo stesso con la build della 0.12.5-gb).

### 0.7 Ritorno alla 0.12.5-gb (ripiego, non un passo)
Per le prove 7 e 12 (memoria) e per ogni guasto che chiede la versione di prima. Spegnere il tema (`send null`) **non
libera niente**: l'arena del `Runtime` è statica e si presta alla partita anche senza tema attivo. Il ripiego vero è
la 0.12.5-gb, che dopo 0.6 è ancora in app0 (la 0.13.0-ed in app1); `/th` e `calamaio-th` la 0.12.5-gb li ignora.
- **Senza cavo** (preferito; la scheda in rete, fuori dalla partita): la build della 0.12.5-gb è quella del ramo
  `giochi`, ancora sul disco.
  ```bash
  strings ~/CalamaiOS-giochi/firmware/.pio/build/heltec213/firmware.bin | grep -m1 calamaio-fw   # calamaio-fw 0.12.5-gb heltec213
  $CAL ota push --board $B --build ~/CalamaiOS-giochi/firmware/.pio/build/heltec213
  ```
  Atteso: «Nota: 0.12.5-gb è più vecchia della versione della scheda (0.13.0-ed)», `s` alla domanda; l'immagine va
  nella partizione libera (app0, sopra la stessa 0.12.5-gb), riavvio, `st` dà `fw 0.12.5-gb`.
- **Col cavo** (solo con un sì dell'utente): rimettere l'otadata del backup 0.3, che punta ad app0:
  ```bash
  cd $R/firmware && dd if=$BK/nvs-otadata-<D>-pre-0.13.0.bin of=$SP/otadata.bin bs=4096 skip=5 count=2 \
    && $ESP write_flash 0xe000 $SP/otadata.bin
  ```
  poi RST. Atteso: `st` dà `fw 0.12.5-gb`.

## Fase 1: temi senza partita (sicure)

### 1. Versione e avvio senza temi
Passi: dopo 0.6, `st` e il registro dall'avvio.
Atteso: `Calamaio OS 0.13.0-ed · …` nel banner; `[temi] 0 sulla scheda, attivo «Calamaio»`; `[temi] themes: 0 sulla
scheda, attivo «Calamaio»` dopo `auth-ok`; `st`: `fw 0.13.0-ed | supported True | have [] | cur None | job None`;
Menu › Display › Tema mostra solo «Calamaio» ✓. `curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8766/themes/0123456789abcdef0123456789abcdef` → `404`.
Se fallisce: `supported False` con `fw 0.13.0-ed` = `themes` non arrivato o scartato: cercare `themes non valido` o
`themes da una connessione non autenticata` in `serve.log`; `[temi] /th non creata` = LittleFS senza `/th`.

### 2. Tema inviato e attivato (download, tempi, file aperti)
Precondizione: Home con almeno una sessione (avatar disegnati: il loop tiene aperti avatar e blocco del tema mentre il
task di rete scarica).
```bash
add $R/protocol/themes/fixtures/prova.theme.json
send '"prova"'; sleep 5; st
```
Atteso: risposta 201 `{"theme":{"id":"prova","name":"Mascotte grande",…,"bytes":675,…}}`, poi 202 con
`stage:"attesa"`/`"offerta"`. Servizio: `temi t…: offerta di prova (675 byte, crc …)`, `… prova scaricato da
192.168.1.58 (675 byte)`, `theme-use`, `… attivo su calamaio-48e8`. Scheda: `[temi] theme-offer prova: 675 byte,
scarico al prossimo giro`, `[temi] theme-ack prova: ok`, `[temi] prova installato (675 byte, crc …) in T ms`
(**annotare T**: conta dal GET alla fine del salvataggio, connessione esclusa), `[temi] themes: 1 sulla scheda…`,
`[temi] attivo «prova»`, nessun errore di file («too many open files», `[avatar]` che non legge). Home con la mascotte
grande; `st`: `have ['prova'] | cur prova | job ('prova', 'fatto', None)`.
Se fallisce: `job` in `errore` con il motivo della scheda (testi in PROTOCOLLO «Temi»); `download non riuscito (…)` =
rete o porta 8766; `salvataggio non riuscito` = la rinomina di LittleFS (vedi 9). Nessun file resta: il temporaneo si
butta.

### 3. Tema già sulla scheda e ritorno a Calamaio dalla webapp
```bash
send null; sleep 3; st; send '"prova"'; sleep 3; st
```
Atteso: con `null` niente offerta, solo `theme-use` → Home di serie, `cur None`, `job (None, 'fatto')`; con `prova` di
nuovo niente offerta né download (stesso CRC: nel servizio nessuna riga «offerta»/«scaricato»), `theme-use` →
`cur prova`, `fatto`.
Se fallisce: un secondo download = `have` non aggiornato (guardare l'ultimo `themes` della scheda nel registro).

### 4. Cambio dal Menu della scheda e scelta salvata
Passi: Menu › Display › Tema › Calamaio (2×); poi di nuovo «Mascotte grande» (2×); poi Info › Riavvia.
Atteso: cambio immediato col ridisegno, ✓ che si sposta, azione «Tema: Calamaio» / «Tema: Mascotte grande»; registro
`[temi] attivo «Calamaio»`, `[temi] themes: 1 sulla scheda, attivo «Calamaio»`, poi «prova»; `st` segue (`cur`).
Dopo il riavvio: `[temi] 1 sulla scheda, attivo «prova»` e la prima Home già a tema (NVS `calamaio-th/cur`).
Se fallisce: dopo il riavvio di serie = `cur` non salvata: annotare e controllare `themefs::saveCur`.

### 5. Ridisegni a tempo: pagine della lista e orologio coi secondi
Passi: con «Mascotte grande» attivo e almeno 4 sessioni (3 righe per pagina; se mancano, finte:
`for s in b8a b8b b8c b8d; do curl -s -X POST -H 'Content-Type: application/json' -d "{\"session_id\":\"$s\"}" http://127.0.0.1:8765/statusline; done`,
da archiviare dopo), guardare la Home 30 s (la lista va a pagine ogni 5 s).
Poi `add $SP/secondi.theme.json; send '"secondi"'`, guardare 1 minuto; lasciare 30 minuti a batteria (senza cavo) in
Attivo/Inattivo e annotare la batteria (`curl -s http://127.0.0.1:8765/api/status | grep -o '"batt":[0-9.]*'`)
all'inizio e alla fine; ripetere 30 minuti col tema di serie (`send null`) per il confronto.
Atteso: un parziale a ogni cambio di pagina (pallini che si spostano) e, con `secondi`, un parziale al secondo con l'ora
`hh:mm:ss` giusta; nessun completo in più oltre la pulizia periodica. **Annotare** il consumo dei due tratti in DECISIONI
(«Limiti noti»): se l'orologio coi secondi costa troppo, decidere con l'utente (per esempio `hms` solo col cavo).
Se fallisce: pagine ferme = `nextChange` non arrivato al loop (`themeWakeAt`): annotare.

### 6. Permesso in arrivo col tema attivo (§7)
Precondizione: `send '"prova"'` fatto, display con i permessi accesi (`perms` vero in `/api/status`).
```bash
curl -s -X POST -H 'Content-Type: application/json' --max-time 120 \
  -d '{"session_id":"prova-b8","hook_event_name":"PermissionRequest","tool_name":"Bash","tool_input":{"command":"echo prova"}}' \
  'http://127.0.0.1:8765/hook?wait=1'; echo
```
(oppure una richiesta vera da Claude Code). Atteso: schermata Permessi **di serie** (NEGA preselezionato, tenuto
3 s), identica a quella senza tema; la risposta scelta torna nel `curl`; dopo, la Home a tema.
Se fallisce: una Permessi a tema è un errore di sicurezza: fermarsi, annotare, tornare a `send null`.

### 7. Memoria col tema attivo
Passi: con «Mascotte grande» attivo e il controller associato (Bluetooth acceso) aspettare 2–3 righe `[memoria]`.
Atteso: `[memoria] heap libero … (minimo …), stack rete libero …` e `[memoria] Bluetooth acceso: …` da confrontare con
0.4. Stima: circa −7,1 KB di heap rispetto alla 0.12.5-gb (RAM statica +7 080). **Annotare** in DECISIONI («Memoria»).
Se fallisce: heap minimo sotto ~15 KB col Bluetooth acceso: annotare e decidere con l'utente prima delle prove di partita
(`send null` non cambia niente: la memoria in più è statica; il ripiego è 0.7, ritorno alla 0.12.5-gb).

### 8. Download grande (tempo contro il ping di 15 s, stack, heap)
```bash
add $SP/grande.theme.json; send '"grande"'; sleep 20; st
```
Atteso: `[temi] grande installato (47628 byte, …) in T ms`, `fatto`, nessuna chiusura del WebSocket durante il
download (il task di rete non risponde ai ping mentre scarica); la riga `[memoria] … stack rete libero N` dopo
l'installazione (atteso ≥ ~6 KB di margine) e lo heap. **Annotare** T: sotto ~5 s va bene; oltre si spezza il download
a pezzi per giro come `otaLoop` (voce «Download nel task di rete» in DECISIONI). Poi `send '"prova"'`.
Se fallisce: `download troppo lento` = più di 15 s per il corpo: annotare la rete; un riavvio (watchdog) = stack:
annotare l'ultima riga e tornare con `send null`.

### 9. Aggiornamento di un tema già sulla scheda (rinomina sopra un file esistente)
Precondizione: «prova» attivo. Aprire Menu › Display › Tema sulla scheda e lasciarlo aperto.
```bash
python3 -c "import json;d=json.load(open('$R/protocol/themes/fixtures/prova.theme.json'));d['name']='Mascotte grande 2';print(json.dumps(d))" > $SP/prova2.json
curl -s -X PUT "${J[@]}" --data-binary @$SP/prova2.json http://127.0.0.1:8765/api/themes/prova; echo
curl -s http://127.0.0.1:8765/api/themes/prova/history; echo
send '"prova"'; sleep 5; st
```
Atteso: PUT 200 col CRC nuovo, cronologia con 2 versioni; offerta e download (CRC diverso), `[temi] prova installato`,
la lista aperta si ridisegna con «Mascotte grande 2» ✓ (tema attivo ricaricato col CRC nuovo), `fatto`, nessun
«salvataggio non riuscito».
Se fallisce: «salvataggio non riuscito» = esp_littlefs non sostituisce un file esistente con `rename`: il tema vecchio
resta, nessuna perdita; annotare in DECISIONI e decidere con l'utente (per esempio rinomina in due tempi con un nome di
riserva).

### 10. Pari merito di `oggi.costo` sulla newlib
Passi: `add $SP/costo.theme.json; send '"costo"'`. Leggere il costo di oggi `C` e portarlo a un pari merito esatto
con una sessione finta della status line (la prima riga fa da base, la seconda aggiunge la differenza):
```bash
C=$(curl -s http://127.0.0.1:8765/api/status | python3 -c 'import json,sys;print(json.load(sys.stdin)["state"]["today"]["cost"])')
T=$(python3 -c "import math;C=$C;print(math.floor(C)+1.125)"); D=$(python3 -c "print($T-$C)")
for u in 0 $D; do curl -s -X POST -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"costo-b8\",\"cost\":{\"total_cost_usd\":$u}}" http://127.0.0.1:8765/statusline; done
```
Aprire Menu › Oggi sulla scheda (controllare prima `st`/`state.today.cost` = `$T`).
Atteso: la scheda scrive lo stesso testo di `printf '%.2f $\n' $T | tr . ,` sul Mac (`%.2f` della libc di macOS, che
`printf2` del JS copia: per 1,125 dà `1,12 $`). Se scrive `…,13 $` la newlib arrotonda diversamente: annotare in
DECISIONI («Limiti noti») e allineare `printf2` alla newlib.
Se `C` intanto cambia (sessioni vere), ripetere. Dopo: archiviare la sessione `costo-b8` dalla webapp.

### 11. Nono tema, ordine e tema protetto
Precondizione: sulla scheda, in ordine d'installazione, `secondi`, `grande`, `prova` (reinstallato in 9: va in fondo),
`costo` (controllare con `st`: 4 temi).
```bash
for i in 1 2 3 4; do add $SP/t$i.theme.json; send "\"t$i\""; sleep 6; done; st      # 8 temi, attivo t4
```
Poi sul Menu della scheda scegliere il più vecchio, «Orologio coi secondi» (2×), e subito dopo:
```bash
add $SP/t5.theme.json; send '"t5"'; sleep 8; st
```
Atteso: con 8 temi nessuna uscita; col nono esce **`grande`** (il più vecchio che non è né attivo né protetto: `secondi`
è appena stato scelto), `have` = secondi, prova, costo, t1…t5 (8), attivo t5 dopo il `theme-use`. Il registro non deve
mai mostrare `secondi` tolto.
Se fallisce: un tema attivo o appena scelto tolto = errore grave: annotare l'ordine visto e fermarsi.

## Fase 2: partita (cavo e monitor seriale per le righe `[gb]`)

### 12. Cartuccia grande col tema attivo (Pokémon Red)
Precondizione: tema «Prova 5» (t5, uguale a «Mascotte grande») attivo, controller collegato, monitor seriale aperto.
Passi: Menu › Gioco › ROM › Pokémon, giocare 1 minuto, un SALVA nel gioco, Salva ed esci.
Atteso: `[gb] RAM cartuccia: 16384 byte dall'anello del registro, 6144 dal tema, 10240 dallo heap` (l'arena del
`Runtime`: blocco + buffer del disegno, tre pezzi da 2 KB); `[gb] memoria per la partita: servono … (+8192 di
margine), liberi …, blocco massimo …` con «servono» 6 144 byte meno della 0.12.5-gb; `[gb] … pronto: … heap libero …`,
`[memoria] partita avviata …` e `[gb] emulatore: … heap libero … (minimo …)` da confrontare con 0.4 (stima: ≈ −0,9 KB
di heap libero rispetto alla 0.12.5-gb). All'uscita la Home del tema torna uguale (`[temi]` nessun «rovinato»).
**Annotare** in DECISIONI («Memoria»): `servono`, `liberi`, **blocco massimo** (il rischio vero è la frammentazione:
`begin` vuole un blocco ≥ contesto di Peanut-GB, ~17 KB, e `fastink::reserve` blocchi da 16 KB), heap libero a
«pronto» e minimo in partita. Con liberi in partita < 15 KB o blocco massimo < ~20 KB si decide con l'utente lo stack di
«gb-ink» nell'arena (proposta 2 della revisione finale).
Se fallisce: «Memoria insufficiente» = annotare `servono`/`liberi`/`blocco massimo`; `send null` **non serve** (l'arena
si presta anche senza tema): per giocare si torna alla 0.12.5-gb (0.7). Un tema rovinato dopo l'uscita = il prestito
non torna pulito (`returnRing`): annotare.

### 13. Tema mandato durante una partita
Passi: avviare una partita; durante, `add $SP/lampeggio.theme.json; send '"lampeggio"'; st`; dopo 1 minuto Salva ed
esci; `sleep 30; st`.
Atteso: durante, `job ('lampeggio', 'attesa', None)` (scheda scollegata, Wi-Fi spento); all'uscita la rete torna e da
soli `themes` → offerta → download → `fatto`, attivo «Riposo che lampeggia» (Home di serie: il tema ha solo il
Riposo). Col nono tema esce il più vecchio non attivo (`secondi`).
Se fallisce: `attesa` che non riparte dopo `auth-ok` = annotare `serve.log`; ritorno con indice vecchio = cercare
`il file è integro (indice vecchio)` (sicuro) o «rovinato» (non deve succedere).

## Fase 3: Riposo in sonno profondo (senza cavo)

### 14. Riposo a tema
Precondizione: tema con la schermata `sleep` attivo (Menu › Display › Tema › «Prova 5»), cavo staccato.
Passi: tenere PRG 3 s sulla Home (Riposo), lasciare 10 minuti; poi, scheda ancora nel Riposo, mandare un aggiornamento
di `t5` dal PC (`python3 -c "import json;d=json.load(open('$SP/t5.theme.json'));d['name']='Prova 5b';print(json.dumps(d))" > $SP/t5b.json; curl -s -X PUT "${J[@]}" --data-binary @$SP/t5b.json http://127.0.0.1:8765/api/themes/t5; send '"t5"'`),
aspettare 3 minuti, poi premere PRG; dopo l'installazione tornare nel Riposo per altri 5 minuti.
Atteso: l'ora del Riposo a tema si aggiorna ogni minuto senza risvegli pieni. Al risveglio, nel registro:
`[orologio] dall'ultimo risveglio pieno: N risvegli dell'ora, … sveglio in media M ms` (**annotare M** e confrontarlo
con lo stesso Riposo col tema di serie, 10 minuti con `send null`: `activateKnown` legge solo intestazione, indice e un
blocco). L'aggiornamento resta in `attesa` finché la scheda dorme (nei risvegli dell'ora la rete resta spenta) e si
installa al risveglio pieno (`t5` attivo ricaricato); il Riposo dopo usa il CRC nuovo e i risvegli dell'ora restano
leggeri. Una riga `[orologio] il tema del Riposo è cambiato o non si legge: risveglio pieno` vuol dire che il file è
cambiato sotto un Riposo salvato: sicuro, un risveglio pieno e poi di nuovo leggeri.
Se fallisce: risvegli pieni a ogni minuto con un Riposo fermo = `RestKeep` senza id (keepPanel) o `mountNoFormat`
fallito (`[orologio] LittleFS non montato`): annotare; mai una formattazione (i salvataggi `/gb` devono restare).

### 14b. Tema senza Riposo
Passi: `send '"secondi"'` (solo Home; uscito in 13, si riscarica e fa uscire `prova`), Riposo 5 minuti senza cavo,
poi PRG.
Atteso: risvegli dell'ora leggeri come col tema di serie (nessun «risveglio pieno» a ogni minuto in `[orologio]`).

### 14c. Riposo che lampeggia
Passi: `send '"lampeggio"'` (o già attivo da 13), Riposo 10 minuti senza cavo, poi PRG.
Atteso (scelta accettata): ogni risveglio dell'ora diventa pieno (il blocco dipende dal fotogramma). **Annotare** il
tempo da svegli e la batteria rispetto a 14: è il costo di un Riposo animato.

## Fase 4: prove rischiose (dopo un backup nuovo)

### 15.0 Backup nuovo (cavo)
Come 0.3, con suffisso `-pre-rischiose`: NVS + otadata e LittleFS (ci sono i salvataggi di 12). Poi di nuovo la
cartella dati del PC come in 0.5 (il servizio del ramo editor intanto ci ha scritto, `themes/` compresa):
```bash
D3=$(date +%Y%m%d-%H%M); mkdir $BK/pc-data-$D3-pre-rischiose && rsync -a --exclude logs "$P/" $BK/pc-data-$D3-pre-rischiose/ \
  && chmod 700 $BK/pc-data-$D3-pre-rischiose
diff -rq --exclude logs "$P" $BK/pc-data-$D3-pre-rischiose && du -sh $BK/pc-data-$D3-pre-rischiose
```

### 15. Uscita di emergenza (PRG all'avvio)
Precondizione: un tema attivo con la Home (`send '"prova"'`: uscito in 14b, si riscarica e fa uscire `costo`). PRG mai tenuto mentre si preme RST o si dà corrente
(GPIO0: caricatore seriale); per i riavvii usare Info › Riavvia.
- **15a.** Info › Riavvia; appena lo schermo comincia il primo disegno dell'avvio, premere PRG e tenerlo ~2 s.
  Atteso: la schermata di avvio resta finché la pressione non arriva a 1,5 s; `[temi] PRG all'avvio: tema di serie per
  questo avvio`; al rilascio `[tasto] … scartato (uscita di emergenza)`; prima Home di serie col toast «Tema di serie»
  (5 s); Menu › Display › Tema: ✓ su «Calamaio». Poi Info › Riavvia senza toccare niente: torna «Mascotte grande».
- **15b.** Come 15a ma premendo prima, mentre lo schermo mostra ancora quello di prima (dopo ~0,5 s da Riavvia, non
  prima: nei primi ~200 ms del setup l'interruzione non è ancora collegata e la pressione non conta). Atteso: come 15a.
- **15c.** Pressione rilasciata a ~1 s. Atteso: nessuna uscita, l'avvio finisce normale, Home a tema, nessuna riga
  «scartato (uscita di emergenza)».
- **15d.** Pressione tenuta oltre 3 s. Atteso: uscita fatta; dopo il rilascio la pressione successiva funziona
  normalmente (non scartata).
- **15e.** Durante un'uscita (dopo 15a) Menu › Display › Tema › Calamaio (2×), poi Info › Riavvia. Atteso: `[temi]
  attivo «Calamaio»`, NVS `cur` vuota: dopo il riavvio resta di serie (`[temi] … attivo «Calamaio»`). Poi `send '"prova"'`.
Se fallisce: nessuna uscita col tasto tenuto = annotare quando è cominciata la pressione; il tema resta sceglibile dal
menu (Calamaio) in ogni caso.

### 16. Tema attivo rovinato sulla flash
Precondizione: 15.0 fatto; attivo un tema installato **una sola volta** e più grande di 512 byte (non sta nei metadati
di littlefs): «Prova 3» (`t3`, 675 byte) dal Menu. Cavo.
```bash
D=$(date +%Y%m%d-%H%M); $ESP read_flash 0x690000 0x160000 $SP/lfs-$D.bin
python3 - $SP/lfs-$D.bin <<'EOF'
import sys
p=sys.argv[1]; b=bytearray(open(p,'rb').read())
hits=[i for i in range(len(b)-24) if b[i:i+6]==b'CTHM\x01\x48' and b[i+20:i+23]==b't3\x00']
print('copie trovate:', [hex(h) for h in hits])
if len(hits)!=1: sys.exit('ci si ferma: serve una sola copia')
o=hits[0]+100; b[o]^=0xFF; s=o//4096*4096
open(p+'.sector','wb').write(b[s:s+4096]); print('settore', hex(0x690000+s))
EOF
$ESP write_flash <settore stampato> $SP/lfs-$D.bin.sector
```
Poi RST (senza PRG). Atteso: `[temi] t3 rovinato: tolto, si usa il tema di serie`, toast «Tema rovinato: uso quello di
serie» sulla prima Home (5 s), `st`: `have` senza `t3`, `cur None`; gli altri temi, gli avatar e i salvataggi `/gb`
intatti. Dopo un altro riavvio resta di serie.
Se fallisce: più di una copia → non scrivere niente (prova saltata, coperta da `test_boot_plan_broken_active`). Se al
riavvio LittleFS non monta o si riformatta (righe `[avatar]`/`[gb]` di formattazione, avatar che tornano dal servizio):
rimettere subito la partizione del backup 15.0 con `$ESP write_flash 0x690000 $BK/littlefs-<D>-pre-rischiose.bin`.

### 17. Ripristino di fabbrica: rimandata (non in questo giro)
Tolta dal giro (revisione finale): il ripristino riscriverebbe a crudo la NVS con esptool, oltre il comportamento
normale dell'app, e rischia l'abbinamento sui due lati; `eraseAll`/`dropAllThemes` sono già provati in native. Se l'utente
la vuole in un giro futuro serve un suo **sì esplicito e separato**, con subito prima un backup nuovo della NVS
(come 0.3) **e** della cartella dati del PC (come 0.5).

## Non provabili dal vivo (coperte dai test)
- `theme-use` di un tema che la scheda non ha: il servizio vero lo manda solo se `have` lo contiene
  (`TestThemeUseUnknownReoffers`, `test_messages`).
- Riavvio fra la rinomina del nono tema e `/th/order` (`test_order_trim_after_crash`); riavvio a metà download (`/th/tmp`
  tolto da `themefs::begin`): il download di un tema dura troppo poco per interromperlo a mano.
- Token 410/403 sul filo (il token non si vede da fuori): `TestThemeTokenMalformed` e gli altri `TestTheme*`.

## Chiusura
- Riportare in DECISIONI (sezione «Editor delle schermate»): T di 2 e 8, heap di 7 e 12, tempi da svegli di 14/14c,
  consumo di 5, esito di 9 e 10.
- Chiedere all'utente se tenere il servizio del ramo editor (`$CAL`, dal suo `serve.log`) o tornare al vecchio
  (`~/.local/bin/calamaio serve --firmware ~/CalamaiOS/firmware/.pio/build/heltec213`); la 0.13.0-ed
  resta sulla scheda (il vecchio servizio non le offre temi e la scheda li ignora).
