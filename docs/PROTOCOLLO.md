# Protocollo servizio ↔ display

Versione **1** (M3, più le impostazioni remote aggiunte con la webapp in modo compatibile: `settings`, `set`, `ack`, `full-refresh`; in M4 «Archivia»: `dismiss`; in M5 abbinamento, autenticazione e permessi firmati, sezione «M5» sotto; poi gli avatar delle sessioni, sezione «Avatar»; in M6 il riepilogo di `bye`; poi aggiornamento remoto, registro da remoto e crash, sezione «OTA», e la posa `run` degli avatar; poi più reti Wi-Fi gestite dal PC, sezione «Reti Wi-Fi»; poi i temi delle schermate, sezione «Temi»; poi (firmware 0.14.0) gli avatar a pezzi, sezione «Avatar»). È la fonte unica: firmware e servizio seguono questo file, `protocol/schema/message.schema.json` e gli esempi in `protocol/examples/`, che entrambi usano nei test.

## Trasporto e scoperta

- **Servizio** (PC): WebSocket su `ws://<ip-del-pc>:8766/ws`, su tutte le interfacce di rete locale. API locale degli hook resta solo su `127.0.0.1:8765`.
- **mDNS**: il servizio annuncia `_calamaio._tcp`, porta 8766, TXT `proto=1` e `path=/ws`. Il display lo cerca per tipo di servizio; il nome host del PC non conta.
- **Display**: si annuncia come `calamaio-xxxx.local` (xxxx = ultime 4 cifre esadecimali del MAC Wi‑Fi, minuscole) con il servizio `_calamaio-board._tcp`, porta 80.
- **Senza mDNS** (firmware 0.12.1-gb): se un tentativo mDNS non trova il PC, il display prova l'**indirizzo manuale** della rete in uso, poi l'**ultimo PC noto** di quella rete (alternati ai tentativi successivi), con una prova TCP di al massimo 2 s prima del WebSocket; il nome a schermo è l'indirizzo stesso. Se mDNS trova un PC, vince mDNS. Una riserva che non risponde fa tornare alla ricerca mDNS con la sua cadenza. L'ultimo PC si memorizza (NVS `calamaio-pc/v1`, una voce per SSID, al massimo 5, esce la più vecchia, scritta solo se cambia) solo dopo `auth-ok`; abbinamento e autenticazione restano identici per ogni indirizzo.
- **HTTP del display** (porta 80, anche in modalità stazione): `GET /api/status` riporta in più `pcManual` (indirizzo manuale della rete in uso, `ip` o `ip:porta`, vuoto se manca) e `pcLast` (ultimo PC noto, `ip:porta`, vuoto se manca). `POST /api/pc` con campo form `host` (`ip` o `ip:porta`: IPv4 letterale, porta 1..65535, predefinita 8766, percorso `/ws`; niente nomi DNS; vuoto = cancella) salva l'indirizzo manuale per la rete in uso → 200 `{"ok":true,"host":"…"}` (`host` senza la porta se è 8766, vuoto se cancellato) · 400 `{"ok":false,"err":"indirizzo non valido: …"}` · 409 `{"ok":false,"err":"il display non è collegato a una rete"}` · 500 `{"ok":false,"err":"salvataggio non riuscito"}`. La pagina della scheda (`/`, stato) ha il campo «Indirizzo del PC» e il pulsante «Salva indirizzo».
- **Un messaggio = un frame di testo** con un oggetto JSON: `t` (tipo) e `v` (versione, 1).
- **Tempi**: sempre millisecondi epoch UTC (interi). Il fuso arriva in `welcome.tz` (stringa POSIX TZ).
- **Vita della connessione**: il servizio manda un ping WebSocket ogni 15 s. Se il display non riceve nulla (messaggi o ping) per 45 s, chiude e considera il PC non raggiungibile. Riconnessione: subito, poi ogni 30 s.
- **Chiusure a raffica** (firmware 0.14.0): dalla terza chiusura del WebSocket entro 20 s dall'apertura nel giro di un minuto, o subito dopo una connessione durante la quale è fallita un'allocazione qualsiasi sulla scheda (il contatore è globale: WebSocket, Bluetooth, display; ci rientra la chiusura 1011 della libreria quando non ha il blocco per un messaggio), il primo tentativo dopo la chiusura aspetta 5 s, poi 10, 20, 30, in silenzio (niente «PC non raggiungibile» se il tentativo seguente riesce). Una connessione che dura un minuto senza allocazioni fallite azzera la raffica; una con un'allocazione fallita no.
- **Sicurezza (M5)**: la specifica è `docs/M5-PERMESSI.md`. Il WebSocket resta in chiaro, ma dopo `hello` un display abbinato si autentica con sfida e risposta; `set`, `full-refresh` e `perm-request` vanno **solo** a una connessione autenticata, e i messaggi dei permessi sono firmati. Senza abbinamento il display riceve solo lo stato (sola lettura).

## Sequenza

```
display → servizio   hello        (paired: il display ha una chiave)
servizio → display   welcome      (paired: il servizio ha una chiave per questa scheda)
servizio → display   state        (completo, subito)
servizio → display   game         (gioco della mascotte: subito, dopo auth-ok e a ogni cambio)
servizio → display   auth-challenge   (solo se il servizio ha una chiave: M5)
display → servizio   auth
servizio → display   auth-ok | auth-fail
display → servizio   settings     (subito dopo hello; solo firmware con impostazioni remote; di nuovo dopo auth-ok con remote: true)
servizio → display   state        (completo, a ogni cambiamento; al massimo uno ogni 250 ms)
display → servizio   settings     (a ogni cambio fatto sul dispositivo)
servizio → display   set          (impostazioni chieste dalla webapp)
display → servizio   settings     (aggiornato) e poi ack
servizio → display   full-refresh (dalla webapp)
display → servizio   ack
display → servizio   dismiss      (Archivia, dal menu della sessione)
servizio → display   ack          (stesso id del dismiss) e, se ok, subito uno state senza la sessione
display → servizio   avatar-need  (avatar dello state o del welcome che non ha in cache)
servizio → display   avatar       (uno per id richiesto)
servizio → display   avatar-offer → display: GET /avatars/<token> → avatar-ack   (al posto di avatar, con hello.caps avatar-dl)
servizio → display   bye          (spegnimento pulito, M6)
display → servizio   status       (OTA: cavo e batteria, subito dopo hello e a ogni cambiamento)
servizio → display   ota-key      (OTA: subito dopo auth-ok)
servizio → display   ota-offer / ota-cancel, log-start / log-stop     (solo autenticato)
display → servizio   ota-status, log, crash                            (solo autenticato)
display → servizio   wifi-list    (reti salvate: dopo auth-ok e a ogni cambiamento)
servizio → display   wifi-add / wifi-forget                            (solo autenticato) → ack
display → servizio   accessory-need / anchors-need → accessory / anchors   (gioco della mascotte)
display → servizio   accessory-wear (scelta dal menu) → game a tutti i display
display → servizio   themes       (temi sulla scheda e attivo: dopo auth-ok e a ogni cambio)
servizio → display   theme-offer → display: GET /themes/<token> → theme-ack     (solo autenticato)
servizio → display   theme-use    (solo autenticato) → themes
```

Il display può mandare `settings` subito dopo `hello`, senza aspettare `welcome`: il servizio lo applica dopo aver risposto a `hello`. Un `settings` o un `ack` mandato **prima** di `hello` viene ignorato.

## Messaggi

### `hello` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `board` | string | `calamaio-3f2a` |
| `device` | string | id del profilo, es. `heltec213` |
| `class` | `S`/`M`/`W`/`L` | |
| `fw` | string | versione firmware |
| `w`, `h` | int | risoluzione |
| `paired` | bool, facoltativo | M5: il display ha una chiave di abbinamento (assente nei firmware senza M5) |
| `caps` | array di string, facoltativo | firmware 0.14.0: capacità; `avatar-dl` = avatar a pezzi (sezione «Avatar»). Il servizio ignora quelle che non conosce |

### `welcome` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `time` | int | ora del PC, ms epoch |
| `tz` | string | es. `CET-1CEST,M3.5.0,M10.5.0/3` |
| `host` | string | nome del PC, solo per mostrarlo |
| `service` | string | versione del servizio |
| `paired` | bool | M5: il servizio ha una chiave per questa scheda. Se è diverso da `hello.paired` il display mostra «Abbinamento: da rifare» |
| `defaultAvatar`, `defaultAvatarCrc` | string, u32, facoltativi | avatar predefinito (sezione «Avatar»), sempre insieme |

### `state` (servizio → display), sempre completo
| Campo | Tipo | Note |
|---|---|---|
| `time` | int | ms epoch dell'istantanea |
| `sessions` | array di Sessione | al massimo 6, nell'ordine in cui mostrarle (ordine di apertura; se sono più di 6 restano le più urgenti); mai le archiviate |
| `limits` | oggetto o `null` | `h5`, `h5Reset`, `d7`, `d7Reset`; ogni coppia può mancare |
| `today` | oggetto | `tasks`, `permDisplay`, `permTerminal`, `waitAvgMs`, `linesAdd`, `linesDel`, `cost` (stimato, USD) |
| `log` | array | ultimi eventi, il più recente per primo, al massimo 20: `t`, `name` (max 31 byte), `text` (max 63 byte) |
| `services` | array | `name`, `ok`, `ms` |
| `prs` | array | `repo`, `n`, `title`, `checks` (`ok`/`ko`/`run`) |
| `agenda` | array | prossime 24 h: `t`, `title` |
| `defaultAvatar`, `defaultAvatarCrc` | string, u32, facoltativi | avatar predefinito, come in `welcome`: un cambio arriva subito con lo `state` successivo |

**Sessione**
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | `session_id` di Claude Code |
| `name` | string | max 31 byte UTF-8 |
| `state` | `working`/`permission`/`input`/`done`/`error`/`idle`/`compact` | |
| `detail` | string | es. `Edit src/app.ts`, `Bash: npm run test`; max 95 byte |
| `since` | int | inizio dello stato o della sessione, ms epoch |
| `attnAt` | int o `null` | da quando aspetta l'utente |
| `ctx` | int o `null` | contesto occupato, % |
| `subs` | int | subagenti attivi |
| `test` | `ok`/`ko`/`run`/`null` | |
| `summary` | string | riepilogo di fine turno, max 127 byte |
| `tasks` | array di `[titolo, stato]` | stato 0 da fare, 1 in corso, 2 fatto; max 12; titolo max 47 byte |
| `error` | string | solo se `state` = `error`: `rate_limit`, `overloaded`, … |
| `avatar`, `avatarCrc` | string, u32, facoltativi | avatar della sessione (sessione → progetto → predefinito) e CRC-32 del suo binario; sempre insieme |

Il display tronca in sicurezza i testi più lunghi dei limiti (al confine di un carattere UTF‑8).

**Dimensione**: il messaggio `state` serializzato non supera **12 KB** (il display accetta frame fino a 15 KB). Con i limiti per campo 6 sessioni piene stanno sotto i 9 KB; se testi pieni di caratteri che JSON scrive come `\uXXXX` lo farebbero sforare, il servizio accorcia titoli dei task e testi del log, poi toglie le voci del log più vecchie, poi i task.

**Da dove arrivano i dati (M4)**: hook e status line di Claude Code, tabella evento → effetto in `service/internal/sessions` e nelle voci M4 di `docs/DECISIONI.md`.

### `bye` (servizio → display, M6)
Il servizio lo manda quando si ferma **in modo pulito**: SIGTERM (logout e spegnimento del Mac arrivano così a un LaunchAgent; `systemctl stop`; `kill`), SIGINT (Ctrl-C) o SIGHUP (terminale chiuso). Ordine allo spegnimento:

1. le richieste di permesso aperte si chiudono con `perm-close` `service` (i hook escono senza decisione: decide il terminale);
2. `bye` a **ogni** display che ha fatto `hello` (autenticato o no: il riepilogo non contiene dati che lo `state` non mostri già); il servizio aspetta le scritture **al massimo 2 s**;
3. da `bye` in poi su quella connessione non parte più nessuno `state` e le connessioni nuove ricevono 503; poi il servizio chiude il WebSocket con 1001 («servizio in arresto») ed esce.

Se il servizio si ferma per un **errore** (porta occupata, crash) o la connessione cade, **niente `bye`**: il display mostra «PC non raggiungibile» e riprova (con l'avvio automatico il servizio riparte da solo).

Il display mostra la schermata «PC spento» (`riepilogo()` del simulatore) e va in sonno profondo; la chiusura del WebSocket che segue `bye` non è un «PC non raggiungibile». Al ritorno del PC si ricollega come sempre (`hello`). Attenzione: anche un **riavvio** del servizio (`calamaio install` che ricarica il LaunchAgent, `launchctl kickstart -k`, `systemctl --user restart`) è uno spegnimento pulito e manda `bye`: il servizio torna dopo pochi secondi, quindi il display non deve dormire per sempre dopo `bye` (risveglio a tempo e nuovo tentativo).

`summary` (dati di `riepilogo()` più le statistiche della giornata). **Solo `off` è obbligatorio**: il display tollera l'assenza di ogni altro campo (servizio in modalità finta, versioni future) e ignora quelli che non conosce.

| Campo | Tipo | Note |
|---|---|---|
| `off` | int | ms epoch dello spegnimento: «PC SPENTO ALLE hh:mm» (ora locale col fuso di `welcome.tz`) |
| `day` | string, facoltativo | giorno locale delle statistiche, `AAAA-MM-GG` |
| `sessionsToday` | int, facoltativo | sessioni con almeno un evento oggi (assente con `serve --fake`) |
| `today` | oggetto, facoltativo | come `state.today`: `tasks`, `permDisplay`, `permTerminal`, `waitAvgMs`, `linesAdd`, `linesDel`, `cost` (stimato, USD) |
| `open` | int, facoltativo | sessioni ancora aperte e non archiviate allo spegnimento (anche oltre le 6 elencate) |
| `done` | int, facoltativo | di queste, finite (`done`): «N sessioni finite» |
| `waiting` | int, facoltativo | di queste, che aspettano l'utente (`permission`, `input`, `error`, come `isAttn`): «N ti aspettavano» |
| `sessions` | array, facoltativo | al massimo 6, scelte e ordinate come nello `state` (le più urgenti, in ordine di apertura); ognuna `name` (≤ 31 byte), `state`, `attnAt` (solo `permission` e `input`: «… dalle hh:mm») |
| `last` | oggetto, facoltativo | ultimo riepilogo di fine turno: `name` (≤ 31 byte), `summary` (≤ 127 byte). È quello della sessione con `since` più recente tra quelle con un riepilogo (il simulatore prende l'ultima in elenco: qui conta l'ora) |

Le sessioni archiviate non contano e non compaiono, come nello `state`. **Dimensione**: il messaggio intero non supera **4 KB**; con i limiti per campo il caso peggiore (6 sessioni e riepilogo pieni di caratteri che JSON scrive come `\uXXXX`) pesa 2 763 byte (`TestByeWorstCaseSize`, limite del test 3 KB). Se servisse, il servizio accorcia `last.summary`, poi toglie sessioni dalla fine di `sessions`, poi `last`.

```json
{"t":"bye","v":1,"summary":{"off":1791738000000,"day":"2026-10-11","sessionsToday":7,
 "today":{"tasks":14,"permDisplay":2,"permTerminal":3,"waitAvgMs":132000,"linesAdd":1240,"linesDel":380,"cost":18.4},
 "open":4,"done":2,"waiting":1,
 "sessions":[{"name":"api-server","state":"done"},{"name":"web-app","state":"permission","attnAt":1791737340000},
             {"name":"firmware-esp32","state":"idle"},{"name":"docs-site","state":"done"}],
 "last":{"name":"api-server","summary":"Endpoint /health aggiunto, 12 test verdi."}}}
```
(`protocol/examples/bye.json`)

## Avatar

Formato, regole e binario CAVT: `docs/AVATAR.md` (fonte unica). Qui solo il filo. Aggiunta compatibile alla v1: tutti i campi nuovi sono facoltativi e un firmware che non li conosce li ignora (il firmware M5 legge `welcome` e `state` con un filtro ArduinoJson che scarta le chiavi sconosciute e tratta `avatar` come tipo sconosciuto).

- **Nello `state`** ogni Sessione porta `avatar` (id) e `avatarCrc` (CRC-32 IEEE del binario CAVT, byte 8‑11 dell'intestazione); in radice, e in `welcome`, `defaultAvatar` e `defaultAvatarCrc` per le schermate senza sessione (avvio, riposo, PC spento). Il servizio li manda sempre tutti e quattro; il predefinito è la Goccia finché l'utente non lo cambia (`calamaio avatar default`, webapp). Gli avatar non passano **mai** dentro `state`, che resta sotto i 12 KB (anche con id e CRC massimi su 6 sessioni: 11 455 byte nel test `TestMessageAvatarsAndSize` con testi pieni di `\uXXXX`).
- **Cache sul display**: chiave `(id, crc)`. Se un id dello `state` o del `welcome` manca, o ha un CRC diverso, il display lo chiede e intanto disegna il predefinito (se l'ha) o la Goccia compilata.

### `avatar-need` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `ids` | array di string | 1–16 id diversi, kebab-case, al massimo 24 caratteri |

```json
{"t":"avatar-need","v":1,"ids":["gatto","demone"]}
```

Il servizio risponde, per ogni id che conosce, con un messaggio `avatar` o, se la scheda ha detto `caps: ["avatar-dl"]`, con un'offerta `avatar-offer` (una alla volta), nell'ordine; gli id sconosciuti (avatar tolto dalla libreria) non hanno risposta e restano col ripiego. Lo stesso avatar con lo stesso CRC non si rimanda alla stessa connessione prima di 3 s (difesa da un display che chiede in un ciclo). Dopo una consegna fallita (`avatar-ack` negativo, nessun `avatar-ack` entro 30 s, connessione chiusa con l'offerta in corso, o connessione chiusa entro 10 s da un messaggio `avatar`) lo stesso avatar (id e CRC) non si rimanda alla stessa scheda (`hello.board`) per 60 s, nemmeno su una connessione nuova: è il ciclo visto dal vivo con la 0.13.0-ed (1011, riconnessione, di nuovo `avatar`). Un `avatar-need` malformato si ignora e si registra nel log; prima di `hello` si ignora. Accettato anche senza autenticazione: gli avatar non sono dati sensibili, come lo `state`.

### `avatar` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | |
| `crc` | u32 | uguale a `avatarCrc` dello `state` e ai byte 8‑11 di `data` |
| `data` | string | binario CAVT v1 in base64 standard con padding |

Un avatar di 10 pose × 4 fotogrammi (con la posa `run`, docs/AVATAR.md §12) pesa 7 072 byte = 9 432 caratteri base64; il frame intero è 9 493 byte con un id di 6 lettere (`examples/avatar.json`, il Demone) [misurato: `TestAvatarExampleIsReal`], sotto i 15 KB del display (`TestAvatarWorstCaseFrame`, 10 pose, id di 24, CRC a 10 cifre). Senza `run` (9 pose): 6 372 byte = 8 496 caratteri, frame di 8 557 byte (`examples/avatar-senza-run.json`).

**Posa `run` e firmware vecchi.** Il firmware ≤ 0.6.0 rifiuta un CAVT con il bit 9 di `poseMask` (`parseAvatar()`, AVATAR.md §12.4) e disegnerebbe la Goccia. Il servizio quindi guarda `hello.fw`: se è più vecchio di **0.7.0** (o vuoto), a quella connessione manda il CAVT **senza** `run` (le 9 pose di prima, byte per byte il binario di prima) e negli `avatarCrc`/`defaultAvatarCrc` di `welcome` e `state` i CRC di quel binario (Goccia `0x0386CC22`, Gatto `0xD6AEBFCB`, Demone `0xC6E4FA9B`, Calamaro `0xC2FB3686`); dal 0.7.0 il CAVT con `run` e i CRC nuovi (`0x33072CEE`, `0xB2432AD7`, `0xA3130345`, `0x2D86BCB3`). Confronto delle versioni a numeri separati da punti (`ota.CompareVersions`; `0.7.0-dev` vale come 0.7.0 o più). Prove: `TestAvatarProtocol` (0.7.0, 0.6.0, 0.3.0). Il servizio non manda mai un frame oltre 15 KB. Esempio vero (il Demone della libreria): `protocol/examples/avatar.json`. Il display verifica `data` come `unpack()` di AVATAR.md §8 (magia, versione, lunghezza, CRC, `poseMask`, codice 11) prima di salvarlo.

### `avatar-offer` (servizio → display, solo con `hello.caps` `avatar-dl`)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id dell'avatar |
| `crc` | u32 | CRC del CAVT, come `avatarCrc` |
| `size` | int | byte del CAVT (2 172–7 072) |
| `token` | string | 16 byte casuali in 32 cifre esadecimali minuscole |

Anche **senza autenticazione**, come `avatar` (gli avatar non sono dati sensibili; una scheda non abbinata resterebbe altrimenti col messaggio `avatar` da 9,5 KB che col Bluetooth acceso non ha il blocco per riceverlo). Un'offerta alla volta per connessione; la successiva parte dopo l'`avatar-ack` o dopo 30 s senza. La scheda costruisce l'URL come per i temi: `http://<host>:<porta>/avatars/<token>`. **Download**: `GET /avatars/<token>` sulla porta dei display → 200 `application/octet-stream`, `Content-Length` = `size`, `X-Calamaio-Crc`, `Cache-Control: no-store`; token monouso, 5 minuti, solo per l'IP della connessione che ha ricevuto l'offerta: sconosciuto o malformato 404, già usato o scaduto 410, IP diverso 403 (il token resta valido); muore anche all'`avatar-ack`, alla chiusura della connessione e allo scadere dei 30 s. La scheda scarica nel task di rete (HTTP/1.0, connessione e GET 5 s, corpo al massimo 15 s, mai oltre `size`) a pezzi da 512 byte in `/av/dl`, con i controlli di `unpack()` (AVATAR.md §8) fatti man mano, controlla che `id` e CRC dell'intestazione siano quelli dell'offerta, poi il loop rinomina il file nella cache (rinomina diretta di LittleFS). Un errore in qualunque punto butta il temporaneo. Un'offerta non valida (id, token, `size` fuori regola) si ignora; con un'altra in corso `avatar-ack` negativo «un altro avatar in arrivo, riprova». Esempio: `protocol/examples/avatar-offer.json`.

### `avatar-ack` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `id`, `crc` | string, u32 | quelli dell'offerta |
| `ok` | bool | salvato nella cache |
| `error` | string, facoltativo | solo con `ok: false`, ≤ 127 byte: i testi dei temi per il download («download non riuscito (HTTP 410)», «download interrotto», «download troppo lento», «più byte di quelli offerti», «scrittura non riuscita», «download incompleto»), un errore del CAVT («checksum errato», …), «l'avatar non è quello offerto», «CRC diverso da quello offerto», «spazio per gli avatar non disponibile», «indirizzo del download non valido», «salvataggio non riuscito», «un altro avatar in arrivo, riprova» |

Il servizio accetta un `avatar-ack` solo per l'offerta in corso su quella connessione (stesso `id` e `crc`); gli altri li ignora («senza un'offerta in corso»). Esempi: `protocol/examples/avatar-ack.json`, `avatar-ack-errore.json`. Un CAVT rotto sul PC (un errore del CAVT, «l'avatar non è quello offerto», «CRC diverso da quello offerto») la scheda non lo richiede più con `avatar-need` fino alla prossima connessione, come un messaggio `avatar` che non passa la validazione; gli errori di rete e di scrittura sì (dopo i 60 s del servizio).

## Impostazioni remote

### Valori
Stesse voci del menu Display e Avvisi del firmware (`core/model.h`, `Settings`).

| Chiave | Valori | Voce del menu |
|---|---|---|
| `homeStyle` | `goccia` / `righe` / `contatori` / `una` | Display › Stile home (conta solo per la classe S, ma si manda sempre) |
| `anim` | `auto` / `raffiche` / `continue` / `spente` | Display › Animazioni |
| `speed` | `profilo` / `massima` | Display › Velocità animazioni |
| `invert` | bool | Display › Inverti colori |
| `rot` | bool | Display › Ruota 180° |
| `night` | bool | Avvisi › Notturno |
| `permOn` | bool | Avvisi › Permessi dal display (da remoto **solo `false`**) |
| `sound` | bool | Avvisi › Cicalino |
| `led` | bool | Avvisi › LED |
| `mute` | `off` / `1h` / `night` | Avvisi › Silenzia: `off` = non silenziato, `1h` = per un'ora, `night` = fino alle 8 |
| `save` | bool | Risparmio energetico (si accende e si spegne anche da remoto) |
| `workStart` | `"HH:MM"` | Strumenti › Giornata di lavoro: inizio (predefinito `09:00`). Solo i firmware con la giornata |
| `workEnd` | `"HH:MM"` | Strumenti › Giornata di lavoro: fine (predefinito `18:00`), sempre la fine del conto alla rovescia |
| `game` | bool | Display › Gioco della mascotte. Solo i firmware con il gioco; la fonte è il servizio (`game.on`, sezione «Gioco della mascotte») |

`workStart` e `workEnd` (sezione «Giornata di lavoro» più sotto) sono un'aggiunta compatibile: un firmware senza la giornata non li manda in `settings` e non li riceve mai in `set` (il servizio risponde 409). Un firmware con la giornata li manda **tutti e due**. Formato fisso `HH:MM` a due cifre, 00:00–23:59, ora locale della scheda (il fuso è quello di `welcome.tz`); vale sempre `workStart < workEnd`, nello stesso giorno.

In `set`, `mute: "1h"` e `mute: "night"` partono dal momento in cui il display lo riceve (come la voce del menu); `off` toglie il silenzio. In `settings` il display riferisce quello in corso: finito il silenzio torna `off` e manda un nuovo `settings`.

### `settings` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `settings` | oggetto | **tutte** le chiavi della tabella sopra |
| `remote` | bool | `true` = il display accetta `set` e `full-refresh`. Dal firmware M5 è `true` solo dopo un `auth-ok` valido |

Si manda subito dopo `hello` e ogni volta che un'impostazione cambia, dal menu o per un `set`. Un display che non manda mai `settings` (firmware M3), o lo manda con `remote: false`, non riceve mai `set` né `full-refresh`: la webapp mostra le sue impostazioni (se ci sono) in sola lettura. Il servizio scarta le chiavi che non conosce (firmware più nuovo) e ignora tutto il messaggio se una chiave nota ha un valore non valido.

### `set` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id del comando, 1–32 caratteri; va ripetuto nell'`ack` |
| `settings` | oggetto | **solo** le chiavi da cambiare (almeno una) |

Il servizio manda `set` (e `full-refresh`) solo a una connessione autenticata (M5); la API risponde 409 altrimenti. Il display applica i valori come se li avesse scelti dal menu (stesse conseguenze: refresh, voce nel registro), poi manda `settings` aggiornato e infine `ack`. Se un valore non è applicabile risponde `ack` con `ok: false` e non cambia niente di quel comando.

**Regola dei permessi** (brief §7): `permOn: true` arrivato da remoto **si rifiuta sempre** (`ack` con `ok: false`, `error` = «permOn: si accende solo dal dispositivo»), anche se il servizio non lo manda mai. `permOn: false` si applica.

### `full-refresh` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | come in `set` |

Il display fa un refresh completo del pannello (come Menu › Display › Refresh completo) e risponde `ack`.

### `ack` (display → servizio per `set`, `full-refresh`, `wifi-add`, `wifi-forget` e `workday`; servizio → display per `dismiss`)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id del comando o della richiesta: quelli del servizio iniziano con `c`, quelli del display con `d`, così gli ack nei due versi non si confondono |
| `ok` | bool | |
| `error` | string | solo se `ok` = `false`: motivo breve in italiano, max 95 byte |

Il servizio aspetta l'`ack` **10 s**. Se non arriva, toglie i valori in attesa e lo segnala nella webapp. Un `ack` tardivo o con un id sconosciuto si ignora.

## Archivia

Una sessione **finita** (`done`), **in pausa** (`idle`) o **in errore** (`error`) si può archiviare: sparisce dagli `state` di tutti i display finché non torna a fare qualcosa, cioè finché il suo stato non diventa `working`, `permission`, `input` o `compact` (per esempio con un nuovo prompt). Il promemoria `idle_prompt` di Claude Code (~60 s dopo la fine del turno) **non** la riaccende. Lo stato «archiviata» lo tiene il servizio, vale per tutti i display e per la webapp, e **si perde al riavvio del servizio** (stato effimero: al riavvio le sessioni ricompaiono dai nuovi eventi).

### `dismiss` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | 2–32 caratteri, inizia con `d` (es. `d3`); va ripetuto nell'`ack` |
| `session` | string | `id` della Sessione nello `state` |

```json
{"t":"dismiss","v":1,"id":"d3","session":"d4"}
```

Il servizio risponde subito `ack` con lo stesso `id`:

```json
{"t":"ack","v":1,"id":"d3","ok":true}
{"t":"ack","v":1,"id":"d1","ok":false,"error":"Si archiviano solo le sessioni finite, in pausa o in errore"}
{"t":"ack","v":1,"id":"d2","ok":false,"error":"Sessione sconosciuta"}
```

Con `ok: true` manda subito dopo uno `state` completo senza la sessione (senza aspettare i 250 ms). Archiviare una sessione già archiviata dà `ok: true`. Un `dismiss` malformato (id senza `d`, `session` mancante) si ignora e si registra nel log, come gli altri messaggi non validi. Nel `log` dello `state` compaiono gli eventi «archiviata» e «riprende». `dismiss` è accettato anche da una connessione non autenticata: archiviare è un'azione solo estetica (la sessione torna appena fa qualcosa).

### Sicurezza delle impostazioni remote
Dal M5 `set` e `full-refresh` valgono solo su una connessione autenticata, nei due versi: il servizio non li manda a un display non autenticato e il firmware li rifiuta senza `auth-ok` (ack negativo «display non abbinato…»). Un servizio falso nella stessa rete non ha la chiave, quindi non ottiene `auth-ok` e non cambia niente. In nessun caso si accende «Permessi dal display» da remoto: il firmware rifiuta `permOn: true` sempre, l'accensione resta un gesto sul dispositivo.

## M5: abbinamento, autenticazione, permessi

Specifica completa, formule e motivi: `docs/M5-PERMESSI.md`. Vettori di prova (chiavi fisse, tutti i mac, casi negativi): `protocol/vectors/crypto-vectors.json`; gli esempi `protocol/examples/pair-*.json`, `auth*.json`, `perm-*.json`, `unpair.json` vengono da lì. Stringhe binarie in esadecimale minuscolo; `mac` = HMAC-SHA256 troncato a 16 byte (32 cifre). `‖` = concatenazione di byte; `␟` = byte 0x1F tra campi di testo (etichetta compresa, senza 0x1F finale).

### Abbinamento (`calamaio pair` o «Abbina» nella webapp)
```
servizio → display   pair-begin    {id, commit}         commit = SHA256(pkS); id 1–32 caratteri
display → servizio   pair-key      {id, pkD}
servizio → display   pair-reveal   {id, pkS}            il display verifica l'impegno e mostra il codice
                     (l'utente scrive sul PC il codice letto sul display)
servizio → display   pair-confirm  {id, mac}            codice giusto: mac = HMAC(K, "pair-s" ‖ T)
display → servizio   pair-done     {id, mac}            dopo 2 pressioni: mac = HMAC(K, "pair-d" ‖ T)
servizio → display   auth-challenge {ns}                subito: la connessione si autentica come le altre
nei due versi        pair-abort    {id, reason}
```
`T = SHA256("calamaio-pair-v1" ‖ board ‖ commit ‖ pkD ‖ pkS)`, `K = HKDF(Z, T, "calamaio-key")`, codice = `HKDF(Z, T, "calamaio-sas")`, primi 4 byte big-endian modulo 1 000 000. Il codice non esce mai dal servizio (né in `/api/status` né nel log): si legge solo sul display. Il servizio lo confronta in tempo costante e concede **una** prova: codice sbagliato → `pair-abort {reason: "codice"}`, nessuna chiave salvata. Finestra di 3 minuti (`reason: "tempo"`), un solo abbinamento in corso per display. Motivi di `pair-abort` dal servizio: `codice`, `tempo`, `annullato` (Ctrl-C o «Annulla»), `mac` (pair-done falso), `chiave` (pkD non valida); dal display: `display`, `tempo`, `impegno`, `chiave`, `mac`. K si salva in `config.json` sotto `paired: [{board, key, created}]` (file 0600); un nuovo abbinamento sostituisce il vecchio.

### Autenticazione, a ogni connessione
```
servizio → display   auth-challenge {ns}                 ns: 16 byte casuali
display → servizio   auth      {nd, mac}                 mac = HMAC(K, "auth-d" ‖ board ‖ ns ‖ nd)
servizio → display   auth-ok   {mac}                     mac = HMAC(K, "auth-s" ‖ board ‖ nd ‖ ns)
servizio → display   auth-fail {reason?}                 mac sbagliato: chiavi diverse, «da rifare»
```
Chiave di sessione `SK = HKDF(K, ns ‖ nd, "calamaio-session")`. Un `auth` senza sfida, o una seconda risposta alla stessa sfida, si ignora.

### Permessi
```
servizio → display   perm-request {id, session, name, tool, arg, at, mac}
                     mac = HMAC(SK, "req" ␟ id ␟ session ␟ name ␟ tool ␟ arg ␟ at)
display → servizio   perm-reply   {id, decision: "allow"|"deny", nonce, mac}
                     mac = HMAC(SK, "rep" ␟ id ␟ decision ␟ nonce)
servizio → display   perm-close   {id, reason, mac}
                     mac = HMAC(SK, "close" ␟ id ␟ reason)      reason: display · terminal · session · service
display → servizio   unpair       {mac}                   mac = HMAC(SK, "unpair" ␟ board)
```
- `perm-request` parte solo per un `PermissionRequest` arrivato con `calamaio hook --wait`, mai per `AskUserQuestion` né `ExitPlanMode`, e solo verso display autenticati con `permOn` vero (tutti quelli che lo sono). `id`: 16 byte casuali. `name` (nome della sessione come nello `state`) ≤ 31 byte, `tool` ≤ 63, `arg` (argomento corto: `npm run test`, `src/app.ts`) ≤ 96, troncati in modo sicuro per UTF-8 e senza caratteri di controllo **prima** del mac. `at`: ms epoch dell'arrivo. Un display che si autentica mentre il hook aspetta ancora riceve le richieste aperte.
- Se nessun display autenticato con i permessi accesi è collegato, il hook risponde subito senza decisione (domanda nel terminale).
- `perm-reply` è accettata solo se viene da una connessione autenticata, il mac è giusto con la SK di quella connessione, il nonce non è già stato visto per quell'id e l'id è ancora in attesa. Altrimenti si scarta e il log dice perché (`perm-reply scartata (connessione non autenticata | campi non validi | mac sbagliato | nonce già visto: risposta ripetuta | id inesistente o non più in attesa)`). Vale la prima risposta.
- **Nessuna scadenza**: la richiesta resta aperta finché risponde un display (`perm-close display`), finché si vede la risposta nel terminale (`terminal`: PostToolUse o PostToolUseFailure dello strumento, PreToolUse di un altro strumento dello stesso agente, nuovo prompt, Stop, StopFailure, diniego letto da status line e transcript, oppure il hook chiuso da Claude Code; Claude Code 2.1.296 **non** chiude il hook quando rispondi nel terminale, quindi lo chiude il servizio rispondendo vuoto), finché la sessione finisce (`SessionEnd`: `session`) o il servizio si ferma (`service`, il hook esce senza decisione). `perm-close` va ai display autenticati che hanno ricevuto la richiesta.
- `unpair` (Menu › Sicurezza › Dimentica abbinamento) cancella K anche dal servizio; senza autenticazione o con un mac sbagliato si ignora.

## OTA: aggiornamento remoto, registro da remoto, crash

Specifica e motivi: `docs/OTA.md`. Aggiunta compatibile alla v1: tutti i messaggi sono nuovi, un firmware che non li conosce li ignora (e il servizio, che non riceve `status`, non gli offre aggiornamenti). Stringhe binarie in esadecimale minuscolo, `mac` = HMAC-SHA256 con la chiave di sessione SK troncato a 16 byte, `␟` = 0x1F come nei permessi (M5). Un firmware che manda `status` implementa anche `ota-*`, `log-*` e `crash` (stesso rilascio).

### `status` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `batt` | int 0–100 o `null` | batteria misurata; `null` = la scheda non lo sa |
| `usb` | bool o `null` | cavo USB collegato; `null` = non lo sa |
| `cal` | bool, facoltativo | calibrazione della batteria in corso |
| `workday` | oggetto, facoltativo | giornata di lavoro: `{on: bool, end: int ms epoch \| null}` (sezione «Giornata di lavoro»). Solo i firmware che la gestiscono; assente = invariata |

Subito dopo `hello` (anche senza autenticazione: non è un dato sensibile) e a ogni cambiamento: il cavo subito, la batteria quando cambia di almeno 1 punto, al massimo un messaggio ogni 10 s. Il servizio lo mostra (webapp, `calamaio status`) e lo usa per dire **prima** se l'aggiornamento può partire; la decisione resta della scheda (OTA.md §1.5: con il cavo sempre; senza cavo solo con batteria ≥ 50% misurata subito prima; se non sa né l'una né l'altra cosa, no).
```json
{"t":"status","v":1,"batt":78,"usb":false,"cal":false}
```

### `ota-key` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `pub` | hex 32 byte | chiave pubblica Ed25519 di firma del PC (`calamaio ota keygen`, `<config>/calamaio/ota-key`) |
| `mac` | hex 16 byte | `HMAC(SK, "ota-key" ␟ pub)`, `pub` in esadecimale |

Il servizio la manda a ogni connessione **subito dopo `auth-ok`** (se la chiave c'è) e di nuovo prima di un'offerta se nel frattempo è cambiata. La scheda la accetta solo autenticata e con il mac giusto, la salva nel vault; **una chiave diversa da quella salvata si accetta solo dopo un nuovo abbinamento** (che cancella la vecchia): altrimenti si ignora e le offerte firmate con la nuova vengono rifiutate («firma non valida»). Esempio vero (seme 32 × 0x01, SK = 32 × 0x02): `examples/ota-key.json`.

### `ota-offer` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string 1–32 `[A-Za-z0-9_-]` | id dell'aggiornamento (il servizio usa `o` + 12 cifre esadecimali) |
| `manifest` | oggetto | `board`, `device`, `version`, `size`, `sha256`, `built`, in quest'ordine |
| `sig` | hex 64 byte | firma Ed25519 della forma canonica del manifesto |
| `url` | string | `http://<ip-del-pc>:8766/ota/<token>`, token di 16 byte (32 cifre) |
| `mac` | hex 16 byte | `HMAC(SK, "ota-offer" ␟ id ␟ sha256)` |

**Manifesto.** `board` = la scheda di destinazione (`hello.board`, il manifesto vale solo per lei), `device` = profilo (`hello.device`), `version` (lettere, cifre, `. + _ -`, ≤ 31), `size` = byte dell'immagine dell'app (`firmware.bin`, partizione OTA), `sha256` dell'immagine, `built` = ms epoch della build (data di `firmware.bin`). **Forma canonica firmata**: `board ␟ device ␟ version ␟ size ␟ sha256 ␟ built`, interi in decimale senza zeri iniziali, nessuna etichetta e nessun separatore finale (`Manifest.Canonical()` in `service/internal/ota/manifest.go`; i campi non possono contenere 0x1F). Esempio vero: `examples/ota-offer.json`; vettori condivisi: `protocol/vectors/ota-vectors.json`.

**Download.** `GET /ota/<token>` sullo stesso server dei display (porta 8766): 200 con `Content-Type: application/octet-stream`, `Content-Length` = `size` esatto, `X-Calamaio-Sha256`, nessun `Range`. Il token vale **una volta sola**, per **5 minuti**, e solo per l'**IP della scheda** che ha ricevuto l'offerta: token sconosciuto 404, già usato o scaduto 410, IP diverso 403 (il token resta valido per la scheda). L'host dell'`url` è l'indirizzo locale della connessione WebSocket della scheda (quello che lei ha già raggiunto). Se l'aggiornamento finisce (annullato, errore) il servizio chiude il download a metà.

**La scheda controlla, nell'ordine** (OTA.md §3.1): autenticazione, `mac`, firma con la chiave salvata, `board` e `device` uguali ai suoi, nessuna richiesta di permesso aperta, niente calibrazione, cavo o batteria. Se no: `ota-status {phase: "rifiutato", error}` con un motivo leggibile, per esempio «firma non valida», «nessuna chiave di firma: riabbina la scheda», «immagine per un'altra scheda», «immagine per un altro modello (x)», «richiesta di permesso aperta», «calibrazione della batteria in corso», «batteria al 38%: collega il cavo USB», «non so se c'è il cavo né quanta batteria resta», «aggiornamento già in corso».

### `ota-status` (display → servizio, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | quello dell'offerta |
| `phase` | `rifiutato` · `ricevo` · `verifico` · `scrivo` · `riavvio` · `fatto` · `errore` | |
| `done`, `total` | int, facoltativi | byte ricevuti e totali (`total` = `size`) |
| `error` | string, facoltativo | solo con `rifiutato` ed `errore`: motivo leggibile in italiano, ≤ 127 byte UTF-8 |

`ricevo` all'inizio e circa ogni 5%; `verifico` (SHA-256 e dimensione), `scrivo` (`esp_ota_end`, `esp_ota_set_boot_partition`), `riavvio` subito prima di riavviare. Dopo il riavvio la versione nuova diventa valida con Wi‑Fi + `auth-ok` entro 120 s: il servizio considera l'aggiornamento **fatto** quando la scheda si riautentica con `hello.fw` uguale a `version` (la scheda può anche mandare `fatto`); se torna con un'altra versione è **errore**, e la versione di prima, appena autenticata, manda `ota-status {phase: "errore", error: "la nuova versione non si è avviata: tornato alla precedente"}` con l'id dell'aggiornamento (va tenuto in RTC/NVS fino a lì). Un `ota-status` con un id che il servizio non segue più (servizio riavviato) resta come esito se è `fatto` o `errore`. Fasi che tornano indietro si ignorano.

Scadenze del servizio [scelte]: senza un primo `ota-status` entro **30 s** dall'offerta → errore «la scheda non ha risposto all'offerta» (firmware senza OTA o scheda occupata); in `ricevo`/`verifico`/`scrivo` senza notizie per **60 s** → errore; dopo `riavvio` la scheda deve tornare entro **3 minuti** (120 s di prova del firmware + riconnessione). Se la connessione cade in `scrivo`, vale come `riavvio`.

### `ota-cancel` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | |
| `mac` | hex 16 byte | `HMAC(SK, "ota-cancel" ␟ id)` |

Solo **prima di `scrivo`** (offerta, `ricevo`, `verifico`): il servizio chiude anche il download. La scheda, se il mac è giusto, abbandona l'immagine parziale (mai avviata) e risponde `ota-status {phase: "errore", error: "annullato dal PC"}`; in `scrivo` o dopo lo ignora. Un solo aggiornamento per scheda alla volta (il servizio rifiuta il secondo con 409).

### `log-start`, `log-stop` (servizio → display, solo autenticato) e `log` (display → servizio)
| Messaggio | Campi |
|---|---|
| `log-start` | `id` (1–32), `follow` (bool), `minutes` (1–60) |
| `log-stop` | `id` |
| `log` | `id`, `lines` (array di string, ≤ 400 righe, ≤ 512 byte l'una, ≤ 8 KB per messaggio), `end` (bool, facoltativo) |

La scheda manda il contenuto dell'anello (~16 KB) in uno o più `log`, con `end: true` nell'**ultimo** oppure seguiti da un `log` con `lines` vuoto (le due forme valgono uguale); con `follow` poi le righe nuove raggruppate ogni ~1 s, finché arriva `log-stop` o passano `minutes` (spegnimento automatico anche sulla scheda). Spento di default. Il servizio accetta `log` solo autenticato e solo per l'id dell'ultima richiesta; toglie i caratteri di controllo, salva in `<dati utente>/calamaio/logs/<board>-<AAAA-MM-GG>.log` (file 0600, cartella 0700, con una riga d'intestazione `--- … ---` per ogni richiesta) e tiene in memoria le ultime 4 000 righe per scheda. Senza `end` (firmware che non lo manda) il contenuto dell'anello si considera finito dopo 3 s senza messaggi.

### `crash` (display → servizio, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `fw` | string | versione che è andata in crash |
| `reason` | string ≤ 255 | causa (`esp_core_dump_get_summary`) o `esp_reset_reason()` (es. `ESP_RST_PANIC`, `ESP_RST_BROWNOUT`) |
| `pc` | string, facoltativo | `0x` + 1–8 cifre esadecimali |
| `backtrace` | array, facoltativo | ≤ 64 indirizzi, `0xPC` oppure `0xPC:0xSP` come li stampa l'IDF |
| `last` | string ≤ 255 o array di ≤ 16 string, facoltativo | ultime righe di registro salvate in RTC (il servizio le salva sempre come array) |
| `elf` | string, facoltativo | `esp_ota_get_app_elf_sha256()`: SHA-256 dell'ELF della build (8–64 cifre); è anche nel descrittore dell'app, `esp_app_desc_t.app_elf_sha256` |
| `task`, `exc` | string, facoltativi | task e causa dell'eccezione |

Appena autenticata dopo un riavvio per crash; poi la scheda cancella il coredump (nessun `ack`). Il servizio salva `<dati utente>/calamaio/crash/<board>-<AAAAMMGG-hhmmss>.json` e `.txt` (0600) e traduce PC e backtrace con `xtensa-esp32s3-elf-addr2line -pfiaC` (toolchain di PlatformIO) se trova l'ELF della stessa build: con `elf` quello con la stessa impronta (esatto), senza quello della stessa versione (con riserva). ELF cercati nel pacchetto firmware del servizio e nell'archivio `<dati utente>/calamaio/elf/<versione>_<sha256>.elf`, riempito a ogni `calamaio ota push` (ultimi 20). Esempi: `examples/crash.json`, `examples/crash-senza-coredump.json`.

## Reti Wi-Fi

La scheda ricorda fino a **5** reti e si collega alla più forte (il firmware decide quale e quando cambiare). Dal PC si vedono, si aggiungono e si dimenticano: `calamaio wifi list|add|forget` e la sezione «Reti Wi-Fi» della pagina Dispositivo. Aggiunta compatibile alla v1: un firmware che non conosce questi messaggi non manda `wifi-list` e non risponde a `wifi-add` (il servizio dopo 10 s lo dice). Vettori condivisi: `protocol/vectors/wifi-vectors.json` (generati da `gen-wifi-vectors.mjs`, SK = `flow.auth.SK` di `crypto-vectors.json`); esempi veri in `examples/wifi-*.json` con SK = 32 × 0x02 e un iv fisso **solo per l'esempio** (vettori in `service/internal/m5/wifi_test.go`). `␟` = 0x1F come nei permessi.

**La password non esce mai in chiaro dal PC**: dal browser arriva al servizio solo su `127.0.0.1` (stesse guardie di Host, origine e JSON delle altre POST, in più il servizio controlla che la connessione TCP venga da un indirizzo di loopback); il servizio non la salva, non la scrive nel log né in `/api/status`, la tiene in memoria solo il tempo di cifrarla (il `[]byte` si azzera; la stringa del JSON decodificato resta al garbage collector di Go) e la manda solo a una connessione **autenticata**, cifrata con una chiave derivata dalla SK di quella connessione. `wifi-list` non contiene mai password.

### `wifi-list` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `networks` | array (≤ 5 dal firmware; il servizio ne accetta fino a 16) | nell'ordine di inserimento |
| `networks[].ssid` | string 1–32 byte UTF-8 | |
| `networks[].current` | bool | la scheda è collegata a questa rete adesso |
| `networks[].rssi` | int −127…0 o `null` | dBm; `null` = non vista nell'ultima ricerca (per la rete in uso: la lettura del momento) |
| `networks[].lastUsed` | int ms epoch o `null` | ultimo collegamento riuscito; `null` = mai |

La scheda la manda **dopo `auth-ok`** e a ogni cambio della lista, della rete in uso o dopo una ricerca. Il servizio la accetta anche da una connessione non autenticata (come `status`: non contiene segreti), tiene l'ultima per scheda (anche mentre la scheda è scollegata) e la scarta tutta se una voce non è valida (registrata nel log come gli altri messaggi non validi).
```json
{"t":"wifi-list","v":1,"networks":[{"ssid":"CasaDiProva","current":true,"rssi":-58,"lastUsed":1791748800000},{"ssid":"Telefono","current":false,"rssi":null,"lastUsed":null}]}
```

### `wifi-add` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string 1–32 | id del comando (il servizio usa `c` + numero, come `set`), ripetuto nell'`ack` |
| `ssid` | string 1–32 byte UTF-8 | in chiaro; senza caratteri di controllo (0x1F separa i campi) |
| `iv` | hex 12 byte | casuale, nuovo a ogni messaggio |
| `ct` | hex 24–79 byte | cifrato ‖ tag di 16 byte |

```
chiave = HKDF-SHA256(ikm = SK, salt = id (byte UTF-8), info = "calamaio-wifi")      32 byte
aad    = "wifi-add" ␟ id ␟ ssid                                                      nessun 0x1F finale
ct     = AES-256-GCM(chiave, iv, password UTF-8, aad) ‖ tag
```
Password: 8–63 byte UTF-8 (frase WPA2), senza byte zero (il servizio rifiuta anche gli altri caratteri di controllo); la chiave PSK di 64 cifre esadecimali e le reti aperte non si aggiungono dal PC. Lo stesso SSID già salvato aggiorna la password (anche con la lista piena). La scheda risponde `ack {id, ok, error?}`; errori: `lista piena` (5 reti e SSID nuovo), `password non valida`, `decifratura fallita` (chiave, aad o tag sbagliati: SK di un'altra connessione, campi alterati), `ssid non valido`, e senza autenticazione «display non abbinato: cambia le impostazioni dal menu del display».

### `wifi-forget` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string 1–32 | come sopra |
| `ssid` | string 1–32 byte | |
| `mac` | hex 16 byte | `HMAC(SK, "wifi-forget" ␟ id ␟ ssid)` troncato a 16 byte |

La scheda risponde `ack` (errori: `mac non valido`, `rete sconosciuta`), poi cancella la rete con la **pulizia profonda** della NVS e **si riavvia** dopo ~3 s: il servizio vede la connessione cadere e la scheda tornare. Se era l'ultima rete la scheda torna in configurazione (portale) e non si ricollega al PC finché non le si dà una rete.

**Lato servizio** [scelte]: un'operazione Wi-Fi alla volta per scheda (la seconda: 409); scadenza **10 s** senza `ack` come per `set` (l'operazione fallisce, la webapp e la CLI lo dicono; un `ack` tardivo si ignora); se la connessione cade con un'operazione in attesa l'esito è subito «la scheda si è scollegata prima di confermare». `wifi-forget` si rifiuta durante un aggiornamento remoto in corso (la scheda si riavvierebbe) e, se la scheda ha già mandato `wifi-list`, per uno SSID che non c'è (404). Dopo un `ack` positivo il servizio aggiorna subito la lista che tiene (rete aggiunta senza segnale, rete tolta), poi vale la prossima `wifi-list`.

## Giornata di lavoro

Conto alla rovescia delle ore che mancano alla fine della giornata di lavoro, **tenuto dalla scheda**: funziona anche a PC spento (Menu › Strumenti), il PC lo guarda e lo comanda. Orari predefiniti 09:00–18:00. **Regola A**: la fine è sempre l'orario di fine (`workEnd`) di quel giorno, anche se si inizia tardi. Aggiunta compatibile alla v1: un firmware che non la conosce non manda `status.workday` né `workStart`/`workEnd` in `settings`, e il servizio non gli manda mai `workday` né gli orari (409 con «il firmware di questo display non gestisce la giornata di lavoro: aggiornalo»).

**Orari**: le impostazioni `workStart` e `workEnd` (tabella «Valori» sopra), con `settings` e `set` come le altre. In un `set` può esserci una sola delle due: il servizio la controlla contro l'altra (quella ancora in attesa, se c'è, altrimenti quella riferita dalla scheda) e rifiuta con 400 un inizio che non viene prima della fine. La scheda ricontrolla e risponde `ack` negativo se non va.

### `workday` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string 1–32 | id del comando (`c` + numero, come `set`), ripetuto nell'`ack` |
| `action` | `"start"` · `"stop"` | `start` avvia la giornata adesso (fine = `workEnd` di oggi); `stop` la ferma |

```json
{"t":"workday","v":1,"id":"c12","action":"start"}
```

La scheda applica il comando come dal menu, manda **subito** un `status` con `workday` aggiornato (anche se non sono passati 10 s dall'ultimo: il limite vale per la batteria) e poi `ack {id, ok, error?}` (`ok: true` senza `error`). Senza autenticazione risponde `ack` negativo come per `set`. Il motivo di un rifiuto è un testo breve della scheda (≤ 95 byte); l'esempio `examples/ack-workday-errore.json` («giornata già finita», `start` dopo l'orario di fine) è solo illustrativo: i testi li decide il firmware.

### `status.workday` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `on` | bool | la giornata è in corso |
| `end` | int ms epoch o `null` | quando finisce il conto alla rovescia; `null` con `on: false` |

La scheda lo mette nello `status` subito dopo `hello`, a ogni cambiamento (avvio e fermata dal menu o dal PC, fine della giornata, azzeramento a mezzanotte) e può ripeterlo negli altri `status`. **Assente = invariato** (il servizio tiene l'ultimo valore ricevuto su quella connessione); `null` vale come assente ma dice che il firmware conosce la giornata. Il servizio non calcola il conto: lo ricava la webapp da `end` e dall'ora del PC (minuti interi per difetto: 6 h 25 min 59 s → «mancano 6 h 25»; sotto il minuto «manca meno di un minuto»). Un `workday` non valido (`on` mancante o non booleano, `end` negativo o non intero) fa scartare tutto lo `status`, come gli altri messaggi non validi; con `on: false` un `end` non nullo si ignora.
```json
{"t":"status","v":1,"batt":78,"usb":false,"cal":false,"workday":{"on":true,"end":1791820800000}}
{"t":"status","v":1,"batt":78,"usb":true,"workday":{"on":false,"end":null}}
```

**Lato servizio** [scelte]: un comando `workday` alla volta per scheda (il secondo: 409); scadenza **10 s** senza `ack`, come `set` (il comando fallisce e l'esito lo dice); un `ack` tardivo si ignora; se la connessione cade il comando in attesa si perde con lei. L'esito va in `workday.last` di `/api/status`, **non** in `error` delle impostazioni. Il servizio non cambia `on`/`end` da solo dopo un `ack` positivo: aspetta lo `status` della scheda (la webapp nel frattempo scrive «aspetto il suo stato aggiornato»). CLI: `calamaio workday start|stop|status [--board B]` e `calamaio workday hours 09:00 18:00 [--board B]` (accetta anche `9:00`, `9.00`, `0900` e li manda come `09:00`).

## Gioco della mascotte

Specifica: `docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md`; regole nel motore `service/internal/game`, stato in `<dati>/calamaio/game.json` (`service/internal/gamesvc`); formati degli accessori (CACC) e delle ancore (CANC): `docs/AVATAR.md` §13–14. Aggiunta compatibile alla v1: tutti i messaggi sono facoltativi, un firmware che non li conosce li scarta come tipi sconosciuti. Gioco **spento di default**.

Sul filo: il servizio manda il riassunto (`game`); il display chiede i disegni che non ha con `accessory-need` (accessorio, chiave `(id, crc)`) e `anchors-need` (ancore di un avatar, chiave `(avatar, crc del CAVT)`), e manda la scelta del menu con `accessory-wear`. Un avatar senza ancore non porta accessori: il display non inventa posizioni. Tutti accettati anche senza autenticazione, come `avatar-need` e `dismiss` (niente dati sensibili; la peggior cosa che un estraneo sulla rete può fare è cambiare l'accessorio indossato).

### `game` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `on` | bool | gioco acceso. Spento: `wear` `null`, `unlocked` vuoto, niente `celebrate` né `tired`; livello ed XP restano (i progressi sono salvati) |
| `level` | int ≥ 1 | |
| `xp` | int | XP totali |
| `xpLevelStart` | int | XP totali con cui si entra nel livello: la barra va da `xpLevelStart` a `xpNext` |
| `xpNext` | int | XP totali per il livello successivo (livello n → n+1: `round(100 × n^1.3)`) |
| `energy` | int 0–100 | |
| `tired` | bool | mascotte stanca (posa `tired`): 3 h senza una pausa saggia |
| `wear` | `{id, crc}` o `null` | accessorio indossato; `crc` = CRC-32 del suo CACC (byte 8–11). Il display lo chiede con `accessory-need` se non l'ha |
| `celebrate` | `{id, title}` | **solo** nel messaggio del momento: traguardo raggiunto (`id` del traguardo, `title` ≤ 24 byte), livello nuovo (`livello-N`, «Livello N») o accessorio speciale installato (`speciale-<id>`, nome dell'accessorio). Festa di 3 s, mai con permesso aperto, silenzio, notturno, risparmio (solo toast): lo decide la scheda |
| `unlocked` | array `{id, name}` | accessori sbloccati per il menu «La mascotte › Accessorio», al massimo 20 (i più recenti, sempre con quello indossato); `name` ≤ 24 byte |
| `special` | `true`, facoltativo | c'è un accessorio speciale da creare (catalogo esaurito, traguardo nuovo, proposta non chiusa con «No»): la scheda mostra il toast «Nuovo accessorio speciale: chiedilo a Claude». Assente altrimenti (anche a gioco spento); sparisce con `accessory add --special` o con «No» |

Quando: subito dopo `welcome` e `state` (a ogni `hello`), dopo `auth-ok`, e a ogni cambio (XP, livello, energia, stanchezza, accessorio, acceso/spento), al massimo uno per evento: il servizio confronta il messaggio con l'ultimo mandato e manda solo se cambia (o se c'è una festa). L'energia cambia di un punto ogni ~2,4 minuti di lavoro [calcolato: 60 / 25], quindi lavorando arriva un `game` ogni pochi minuti; il servizio controlla ogni minuto.
```json
{"t":"game","v":1,"on":true,"level":3,"xp":412,"xpLevelStart":346,"xpNext":763,"energy":72,"tired":false,"wear":{"id":"cappellino","crc":4230878407},"unlocked":[{"id":"cappellino","name":"Cappellino"}]}
```
Esempi: `examples/game.json`, `game-festa.json`, `game-spento.json`.

### `accessory-need` (display → servizio) e `accessory` (servizio → display)
`accessory-need {ids}`: 1–16 id diversi (kebab-case, ≤ 24). Il servizio risponde con un `accessory {id, crc, data}` per ogni id che conosce (`data` = CACC v1 in base64, ≤ 1 024 byte = 1 368 caratteri); gli sconosciuti non hanno risposta. Lo stesso accessorio con lo stesso CRC non si rimanda alla stessa connessione prima di 3 s. Il display verifica `data` come `unpackAccessory()` (magia, versione, lunghezza, CRC, limiti) prima di salvarlo. Esempio vero: `examples/accessory.json` (il cappellino, 117 byte).

### `anchors-need` (display → servizio) e `anchors` (servizio → display)
`anchors-need {avatars: [{id, crc}]}`: 1–16 avatar con il CRC del CAVT che il display ha in cache. Il servizio risponde con `anchors {avatar, avatarCrc, data}` (`data` = CANC v1 in base64, 420 byte con 10 pose × 4 fotogrammi = 560 caratteri) **solo se** le sue ancore sono per lo stesso CRC; per un CRC diverso non risponde (il display riceverà prima l'avatar nuovo dallo `state` e chiederà di nuovo), e nemmeno per un avatar senza ancore. Il display tiene il blocco accanto al CAVT con la stessa chiave. Esempio vero: `examples/anchors.json` (il Gatto).

### `accessory-wear` (display → servizio)
`{id}` con l'id di un accessorio, oppure `{"id": null}` per nessuno (il campo è obbligatorio). Il servizio lo applica se l'accessorio è sbloccato e nella libreria e manda `game` a tutti i display; altrimenti rimanda `game` (con l'accessorio vero) solo a chi l'ha chiesto. Nessun `ack`.

### Impostazione `game`
In `settings` (solo i firmware con il gioco) e in `set` (come le altre: solo a una scheda autenticata con `remote`). **La fonte è il servizio** (`game.on`): un `settings.game` diverso da quello riferito prima **sulla stessa connessione** è una scelta dal menu e il servizio accende o spegne il gioco (e manda `game` a tutti); il primo `settings` di una connessione non cambia niente, e la scheda si allinea a `game.on`. Dalla webapp e da `calamaio game on|off` cambia il servizio, e la scheda lo sa da `game`.

### Proposta dell'accessorio speciale (Claude Code)
Col catalogo esaurito, un traguardo nuovo apre una proposta (`SpecialPending`, una alla volta). Il hook `UserPromptSubmit` sincrono del plugin (`calamaio hook --context`, timeout 3 s, client 1 s) chiama `POST /hook/context`: se il gioco è acceso, c'è una proposta e oggi non è già stata fatta per quel traguardo, risponde 200 `{additionalContext}` con l'istruzione per Claude (AskUserQuestion «Creo un accessorio nuovo e speciale per la mascotte?» Sì / Più tardi / No; Sì → skill `calamaio:accessorio`) e la segna come fatta oggi; altrimenti 204. «Più tardi» (`calamaio game special later`) e una proposta senza risposta tornano al primo prompt di un altro giorno; «No» (`calamaio game special decline`) chiude la proposta per quel traguardo. `calamaio accessory add <file> --special` aggiunge l'accessorio (con `"special": true` nel file), lo sblocca, chiude la proposta e lo fa indossare (festa `speciale-<id>`).

### Eventi delle sessioni → gioco (servizio)
Ogni hook è un'`activity` (tempo ed energia, 0 XP). In più, con una chiave che li rende unici anche dopo un riavvio (salvata in `game.json` appena contata): `TaskCompleted` → `task_done` (`task|<sessione>|<task_id>`), un todo di `TodoWrite` che diventa completato → `task_done` (`todo|<sessione>|<impronta del testo>`), `Stop` dell'agente principale con un riepilogo → `turn_done` (`turn|<sessione>|<prompt_id>`, o l'impronta del messaggio finale), esito di un test (`test_regex`) → `test_fail` / `test_ok` / `test_green` (verde dopo un rosso nella stessa sessione; `test|<sessione>|<tool_use_id>`), `SessionEnd` di una sessione finita o archiviazione di una sessione finita → `session_closed` (`closed|<sessione>`, conta una volta sola). Mai punti da permessi, costi, token, notte (00–06) o ore filate.

## Temi

Formato del tema (JSON e binario CTHM v1) e regole di disegno: `docs/TEMI.md` (fonte unica); qui solo il filo, e in
TEMI §8 cosa fa la scheda. Aggiunta compatibile alla v1 (firmware 0.13.0-ed): i messaggi sono nuovi e facoltativi; un
firmware che non li conosce li ignora e il servizio, che non riceve `themes`, non gli offre temi. Niente `mac`: come
`set` e `workday`, offerta e `theme-use` partono solo verso una connessione **autenticata** (M5), e il servizio ignora
`themes` e `theme-ack` da una connessione non autenticata. Permessi, abbinamento e schermate di sistema restano sempre
quelli di serie.

### `themes` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `have` | array di `{id, crc}` | i temi sulla scheda (0–8); `crc` = CRC del CTHM (byte 8–11 dell'intestazione) |
| `cur` | string o `null` | tema attivo; `null` = Calamaio (di serie) |

Dopo `auth-ok` e a ogni cambio: tema installato, scelto dal menu o con `theme-use`, rovinato e tolto, `theme-use` di un
tema che la scheda non ha (`themes` invariato). In modalità gioco (Wi-Fi spento) non parte: la scheda lo manda dopo il
prossimo `auth-ok`. Controllo severo come lo schema: `have` e `cur` obbligatori, nessun altro campo, al massimo 8 voci
di esattamente `id` e `crc` (u32), id col pattern dello schema; un `themes` che non passa si scarta tutto (nel registro
«themes non valido»). La scheda non manda voci con un id non valido e manda `cur: null` se l'id attivo non è valido.

### `theme-offer` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id del tema (kebab-case minuscolo, 1–24, niente `--`) |
| `crc` | u32 | CRC del CTHM |
| `size` | int | byte del CTHM (72–65 536) |
| `token` | string | 16 byte casuali in 32 cifre esadecimali minuscole |

La scheda costruisce l'URL da sola: `http://<host>:<porta>/themes/<token>`, con host e porta del WebSocket a cui è
collegata (di solito la 8766); il messaggio non porta un indirizzo. Un'offerta non valida (id, token, `crc` o `size`
fuori regola) si ignora; da una connessione non autenticata `theme-ack` con `ok: false` e «display non abbinato: cambia
le impostazioni dal menu del display»; con un'altra in corso `ok: false`, «un altro tema in arrivo, riprova».

**Download.** `GET /themes/<token>` sulla porta dei display → 200 `application/octet-stream`, `Content-Length` = `size`,
`X-Calamaio-Crc` (8 cifre esadecimali), `Cache-Control: no-store`. Token monouso, valido 5 minuti, solo per l'IP della
scheda che ha ricevuto l'offerta: sconosciuto o malformato 404, già usato o scaduto 410, IP diverso 403 (il token resta
valido per la scheda). Il token muore anche al `theme-ack` positivo, alla chiusura della connessione e quando
l'operazione è sostituita o finisce. Una `HEAD` lo consuma come una GET (come l'OTA). La scheda scarica nel task di
rete (HTTP/1.0, connessione e GET 5 s ciascuno, poi al massimo 15 s per il corpo, mai oltre `size`) in `/th/tmp`,
controlla lunghezza, CRC, intestazione (stesso `id` e stesso `crc` dell'offerta) e ogni blocco con l'interprete in sola
lettura, poi rinomina in `/th/<id>.cthm` (rinomina diretta di LittleFS sopra il file vecchio, mai `remove` prima); solo
dopo, se i temi sono diventati 9, toglie il più vecchio non attivo (TEMI §8). Un errore in qualunque punto butta il
temporaneo: i temi sulla scheda restano quelli di prima.

### `theme-ack` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | quello dell'offerta (obbligatorio, mai `null`: con un id non valido la scheda non manda niente) |
| `ok` | bool | |
| `error` | string, facoltativo | solo con `ok: false`, ≤ 127 byte (tagliato al confine di un carattere UTF-8). Testi: «display non abbinato: cambia le impostazioni dal menu del display», «un altro tema in arrivo, riprova», «download non riuscito (HTTP 410)» (o il motivo di HTTPClient fra parentesi), «download interrotto», «download troppo lento», «download incompleto», «più byte di quelli offerti», «scrittura non riuscita», «spazio per i temi non disponibile», «memoria insufficiente», un errore del controllo («CRC sbagliato», «blocco di una schermata non valido», «carattere che la scheda non ha», …), «il tema non è quello offerto», «CRC diverso da quello offerto», «salvataggio non riuscito» |

Il servizio accetta un `theme-ack` solo dalla connessione autenticata che ha ricevuto l'offerta, con lo stesso `id` e
con l'operazione in `offerta` o `scaricato`; gli altri li ignora (registro «senza un'offerta in corso»).

### `theme-use` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string o `null` | tema da attivare; `null` = Calamaio. Obbligatorio |

La scheda attiva il tema se ce l'ha (lo salva nella NVS `calamaio-th/cur`) e risponde `themes`; se non ce l'ha resta
com'è e risponde `themes` invariato. Da una connessione non autenticata, o con un id non valido, si ignora.

**Servizio.** `POST /api/displays/{board}/theme` avvia un'operazione per scheda (una alla volta: la nuova sostituisce
la vecchia, il suo token muore). Fasi (`stage`): `attesa` (scheda scollegata, non autenticata o senza `themes` su
questa connessione) → `offerta` → `scaricato` (GET servita o `theme-ack` positivo) → `uso` (`theme-use` mandato) →
`fatto` quando `themes` ha il tema attivo con lo stesso CRC; oppure `errore`: `theme-ack` negativo («la scheda ha
rifiutato il tema: <motivo>»), nessun `theme-ack` entro 60 s dall'offerta («la scheda non ha risposto all'offerta del
tema»), nessun `themes` col tema attivo entro 15 s dal `theme-use`, o la scheda che dopo il `theme-use` non ha il tema
neanche dopo 2 riofferte («la scheda non ha attivato il tema»). Un tema già sulla scheda con lo stesso CRC non si
riscarica (subito `theme-use`); uno già attivo chiude in `fatto`. Un solo `theme-use` per offerta: un `themes` che ha il
tema ma non ancora attivo si aspetta. Se la scheda si scollega a metà, l'operazione torna in `attesa` e riparte al
ritorno (dopo `auth-ok` e `themes`) con un token nuovo; le riofferte dopo una riconnessione non contano nel limite di
2. L'operazione vive nella memoria del servizio: dopo un riavvio del servizio si rimanda dalla webapp. Esempi:
`protocol/examples/theme*.json`.

## API locale della webapp

Non fa parte del protocollo col display, ma ne espone lo stato. Solo su `http://127.0.0.1:8765`, stessa origine della webapp (file inclusi nel servizio).

- **Ogni rotta** di webapp e `/api` risponde 403 se l'`Host` non è `127.0.0.1`, `::1` o `localhost` (difesa dal DNS rebinding) e manda `Content-Security-Policy` (`default-src 'self'`, niente inline, `frame-ancestors 'none'`), `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`, `Cross-Origin-Resource-Policy` e `Cross-Origin-Opener-Policy` `same-origin`.
- **Le POST** vogliono `Content-Type: application/json` (415 altrimenti) e rispondono 403 se `Origin` non è la stessa origine o `Sec-Fetch-Site` non è `same-origin`. Eccezione: una POST **senza corpo** e senza `Content-Type` è ammessa solo se il browser dimostra la stessa origine (`Origin` o `Sec-Fetch-Site`).
- Gli errori hanno sempre la forma `{"error": "testo in italiano"}`.

| Rotta | Risposta |
|---|---|
| `GET /` e file statici | la webapp; `Cache-Control: no-cache` con `ETag` |
| `GET /api/status` | `{service: {version, host, tz, wsPort}, displays: [Display], paired: [board], permissions: [Permesso], state: <messaggio state senza t e v>, autostart: Autostart}`; in `state.sessions` ci sono **tutte** le sessioni, archiviate comprese, ognuna con `archived: bool` in più (oltre 6 se ce ne sono) |
| `GET /api/events` | `text/event-stream`: evento `status` con lo stesso JSON all'apertura e a ogni cambiamento (al massimo uno ogni 250 ms), commento `: keepalive` ogni 15 s |
| `POST /api/displays/{board}/settings` | corpo `{chiave: valore}` → 202 `{id, pending}` · 400 chiave o valore non validi, `permOn: true` («Per accendere i permessi dal display usa il tasto sul dispositivo»), `workStart` non prima di `workEnd` · 409 display non collegato o senza impostazioni remote, orari a un firmware senza la giornata di lavoro |
| `POST /api/displays/{board}/workday` | corpo `{action: "start" \| "stop"}` (niente altri campi) → 202 `{id, pending: WorkdayPending}`; l'esito arriva dopo in `workday.last` · 400 · 409 scheda non collegata o non autenticata, firmware senza la giornata, comando già in attesa · 503 servizio in arresto |
| `POST /api/displays/{board}/full-refresh` | 202 `{id}` · 409 (anche: display non autenticato) |
| `POST /api/displays/{board}/pair/start` | corpo `{}` → 202 `{board, pairing}` · 409 display non collegato o abbinamento già in corso |
| `POST /api/displays/{board}/pair/code` | corpo `{code: "175397"}` (spazi e trattini ammessi) → 202 `{board, pairing}` (codice giusto: si aspetta la conferma sul display) · 400 `{error, aborted: true}` codice sbagliato, abbinamento annullato · 400 `{error}` non sono 6 cifre (nessun annullamento) · 409 nessun abbinamento in corso o codice non ancora mostrato |
| `POST /api/displays/{board}/pair/cancel` | → 202 `{board, pairing}` · 409 nessun abbinamento in corso |
| `POST /api/displays/{board}/unpair` | → 200 `{board, unpaired}`; la scheda può non essere collegata. Le sue connessioni si chiudono: al ritorno il display vede `welcome.paired` falso |
| `POST /api/sessions/{id}/archive` | corpo vuoto o `{}` → 202 `{id, archived: true}` · 404 sessione sconosciuta · 409 stato non archiviabile (working, permission, input, compact) |
| `POST /api/sessions/{id}/unarchive` | → 202 `{id, archived: false}` · 404 |
| `GET /api/avatars[?session=ID]` | `{avatars: [Avatar], free: [id], default, current, problems: [{file, errors}], userDir, rev}`: stessa forma di `calamaio avatar list --json`. «In uso» rispetto alla sessione `session` (che non conta) |
| `GET /api/avatars/{id}` | il file `.avatar.json` com'è (per disegnarlo), header `X-Calamaio-Crc` ed `ETag` = CRC del binario · 404 |
| `POST /api/avatars[?replace=1]` | corpo = il file avatar JSON (stesse guardie: `application/json`) → 201 `{avatar: Avatar, warnings, replaced, file}` · 400 `{error, errors, warnings}` non valido (regole di AVATAR.md §4) · 409 id già presente senza `replace`, o id di un predefinito (mai sostituibili) · 413 oltre 64 KB |
| `POST /api/avatars/default` | `{id}` (`null`, `""` o `"goccia"` = la Goccia) → 200 `{default}` · 400 avatar sconosciuto |
| `POST /api/sessions/{id}/avatar` | `{id}` (`null` o `""` = togli) → 200 `{session, known, avatar, from}` · 400. La sessione può non essere ancora nota (servizio appena riavviato) |
| `POST /api/projects/avatar` | `{dir, id}`: `dir` assoluta; vale per il progetto di `dir` (radice git che la contiene, altrimenti `dir`) → 200 `{dir: progetto, avatar, from}` · 400 |
| `GET /api/avatars/{id}/anchors` | `{avatar, avatarCrc, from, anchors}`: ancore dell'avatar (`from` = `json` o `ancore/<id>.anchors.json`), `anchors: null` se non ne ha · 404 avatar sconosciuto |
| `GET /api/game[?days=N]` | **Gioco**: `{on, level, xp, xpLevelStart, xpNext, energy, tired, streak, todayXP, dailyCap, wear, unlocked, specialPending: Traguardo \| null, proposal: {for?, asked?, answer?}, history: [{day, xp}] (N giorni, 30 se manca, dal più vecchio), achievements: [Traguardo], accessories: [Accessorio], problems: [{file, errors}]}`; stessa forma di `calamaio game status --json` · 404 servizio senza gioco |
| `POST /api/game` | `{on: bool}` → 200 Gioco · 400 |
| `POST /api/game/wear` | `{id}` (`null` o `""` = nessuno) → 200 Gioco · 404 accessorio sconosciuto · 409 non sbloccato |
| `POST /api/game/special` | `{answer: "later" \| "decline"}` → 200 Gioco · 400 · 409 nessuna proposta aperta |
| `GET /api/accessories/{id}` | il file `.accessory.json` com'è (anteprima), `ETag` = CRC del CACC · 404 |
| `POST /api/accessories[?replace=1][&special=1]` | corpo = il file accessorio JSON (`application/json`, ≤ 256 KB) → 201 `{accessory: Accessorio, warnings, special, file}`; con `special` il file deve avere `"special": true` e l'accessorio si sblocca, chiude la proposta e si indossa · 400 `{error, errors, warnings}` (regole di AVATAR.md §14.1, stessi testi di `accessory validate`) · 409 id già presente senza `replace` o id di un predefinito |
| `GET /api/themes` | **Temi** (archivio sul PC, `<dati utente>/calamaio/themes`; senza archivio tutte le rotte dei temi danno 404 «temi non disponibili») → 200 `{themes: [Tema], dir}`, per id |
| `POST /api/themes` | corpo = il file del tema JSON (`application/json`, ≤ 256 KB) → 201 `{theme: Tema}` · 400 `{error, errors: [{path, msg}]}` non valido (regole di TEMI.md; anche l'id `calamaio`) · 409 id già usato · 413 oltre 256 KB |
| `POST /api/themes/check` | corpo = tema JSON (`application/json`, ≤ 256 KB), **senza salvare**: → 200 `{theme: Tema}` sempre, anche non valido o JSON rotto (`problems` e `warnings` dentro, `screens` con i byte per schermata quando compila) · 413 oltre 256 KB. Raggiungibile dal telefono. |
| `POST /api/themes/import` | come sopra, ma un id già usato diventa `<id>-2`, `<id>-3`… (accorciato a 24 caratteri; il nome resta) → 201 `{theme}` · 400 · 409 oltre `-99` |
| `GET /api/themes/{id}` | il file com'è salvato (JSON indentato), `ETag` e `X-Calamaio-Crc` = CRC del CTHM (8 cifre esadecimali), `X-Calamaio-Updated` = ms epoch dell'ultimo salvataggio (la `base` del salvataggio automatico), `Cache-Control: no-cache` · 404 |
| `PUT /api/themes/{id}` | corpo = tema JSON con lo stesso `id` del percorso → 200 `{theme}` (versione nuova nella cronologia; un id che non c'è ancora si crea) · 400 non valido o id diverso dal percorso · 404 id malformato · 409 `calamaio` |
| `PUT /api/themes/{id}?autosave=1&base=<ms>[&fresh=1]` | come la `PUT`, per il salvataggio automatico dell'editor: se `base` > 0 e `updated` del file vivo è diverso (controllo dentro il lucchetto dell'archivio) → 409 `{error: "il tema è stato cambiato altrove", theme: Tema}` con il Tema del file vivo, e niente si scrive; `base` > 0 su un tema che non c'è più → 404; `base` assente o 0 = nessun controllo. Il salvataggio automatico non fa una versione per volta: se la versione più recente l'ha creata un altro salvataggio automatico meno di 2 minuti prima, si riscrive quella (la finestra parte dalla creazione della versione e non scorre); un salvataggio manuale chiude la finestra. Con `&fresh=1` («Sovrascrivi» dopo un conflitto) la finestra si chiude prima di scrivere: nasce sempre una versione nuova, così quella dell'altro dispositivo resta nella cronologia e si può ripristinare; il controllo della `base` vale lo stesso. 400 `base` non numerica; la schermata Permessi (e le altre bloccate) resta rifiutata da ogni strada |
| `DELETE /api/themes/{id}` | senza corpo, solo dalla stessa origine (`Origin` o `Sec-Fetch-Site`; altrimenti 403, con un corpo 400) → 204, file e cronologia tolti · 404 · 409 `calamaio` |
| `GET /api/themes/{id}/history` | 200 `{versions: [{n, at, bytes}]}`, la più recente per prima (al massimo 20) · 404 |
| `POST /api/themes/{id}/restore/{n}` | corpo vuoto o `{}` → 200 `{theme}`: la versione `n` diventa una versione nuova · 404 tema o versione sconosciuti |
| `POST /api/themes/{id}/export` | corpo vuoto o `{}` → 200 il file come allegato (`Content-Disposition: attachment; filename="<id>.theme.json"`) · 404 |
| `POST /api/displays/{board}/theme` | `{id: "tema" \| null}` (`null` = Calamaio, nessun altro campo) → 202 `{theme: ThemeJob}`; l'esito in `/api/status` (`displays[].themes.job`) · 400 corpo · 404 tema sconosciuto · 409 scheda mai vista (né collegata né abbinata), tema non valido, tema per un altro schermo. Una scheda abbinata ma scollegata è accettata: l'operazione aspetta in `attesa` |
| `GET /api/firmware` | `{available, version, chip, parts: [{name, offset, url, size, sha256}], dir, built?, problem?}` |
| `POST /api/displays/{board}/ota` | corpo `{}` → offre il pacchetto firmware del servizio: 202 `{ota: Ota, version, versionSource, device, elf}` · 409 scheda non collegata o non autenticata, chiave di firma assente, aggiornamento già in corso, permesso aperto, profilo sbagliato, pacchetto non valido · 503 servizio in arresto |
| `POST /api/displays/{board}/ota/image?version=V&device=D[&built=MS][&source=S]` | corpo = `firmware.bin` (`application/octet-stream`, stesse guardie, ≤ 2 359 296 byte) → 202 `{ota}` · 400 immagine non valida o versione/profilo che non compaiono nell'immagine · 409 come sopra · 413. È la strada di `calamaio ota push FILE|--build DIR` |
| `POST /api/displays/{board}/ota/cancel` | → 202 `{ota}` · 409 nessun aggiornamento in corso, o già in `scrivo`/`riavvio` («non si può più annullare») |
| `POST /api/displays/{board}/wifi/add` | corpo `{ssid, password}` (niente altri campi; la password non è mai ripetuta nelle risposte) → dopo l'`ack` della scheda: 200 `WifiResult` · 400 `{error}` SSID o password non validi (controllati prima di mandare) · 403 anche se la connessione non viene da un indirizzo di loopback · 409 scheda non collegata o non autenticata, operazione Wi-Fi già in corso, oppure `WifiResult` con `ok: false` (rifiutata dalla scheda: `reason` = il suo motivo; scheda scollegata prima di confermare) · 504 `WifiResult` con `timeout: true` (nessun `ack` in 10 s) · 503 servizio in arresto |
| `POST /api/displays/{board}/wifi/forget` | corpo `{ssid}` → 200 `WifiResult` con `reboot: true` (e `wasLast: true` se era l'ultima rete: la scheda torna in configurazione) · 400 · 404 rete che la scheda non ha · 409 come sopra, anche durante un aggiornamento remoto · 504 |
| `POST /api/displays/{board}/log/start` | `{follow, minutes}` (minuti 1–60, 0 = 10) → 202 `{log: Log}` · 400 · 409 non collegata o non autenticata |
| `POST /api/displays/{board}/log/stop` | → 202 `{log}` · 409 nessun registro in corso |
| `GET /api/displays/{board}/log?after=N[&wait=S]` | `{lines: [{seq, at, text}], next, log}`: righe dopo `after`; con `wait` (≤ 25 s) e nessuna riga nuova aspetta la prossima finché il registro è attivo |
| `GET /api/displays/{board}/log/file[?day=AAAA-MM-GG]` | il file salvato (oggi se manca `day`), `text/plain` come allegato · 404 |
| `GET /api/crashes` | `{crashes: [Crash], dir}`, i più recenti prima (al massimo 50) |
| `GET /api/crashes/{id}` | il rapporto leggibile (`text/plain`) · 404 |
| `GET /api/firmware/parts/{nome}` | il file (`application/octet-stream`, header `X-Calamaio-Sha256`) |

**Avatar** in `/api/avatars`: `id`, `name`, `description`, `author`, `state` (`libero` · `in-uso`), `usedBy` (nomi delle sessioni), `default` (è il predefinito), `source` (`predefinito` · `utente`), `frames`, `poses`, `crc`, `bytes` (binario), `warnings` (avvisi del validatore). In `/api/status`: `avatars: {default, rev}` (`rev` cambia quando cambiano libreria o predefinito) e in ogni sessione `avatar`, `avatarCrc`, `avatarFrom` (`sessione` · `progetto` · `predefinito`), `project` e `cwd`; in `state` anche `defaultAvatar` e `defaultAvatarCrc`. `cwd` e `project` non vanno mai ai display.

**Traguardo** in `/api/game`: `{id, title, description, at}` (`at` = quando è stato raggiunto, `null` se no). **Accessorio**: `{id, name, author, description, special, source (predefinito · utente), crc, bytes (CACC), unlocked, worn}`; i predefiniti nell'ordine di sblocco del catalogo, poi quelli dell'utente per id.

**Hook UserPromptSubmit sincrono**: stessa rotta `POST /hook/context` con il payload di `UserPromptSubmit` → 200 `{additionalContext}` con la proposta dell'accessorio speciale (sezione «Gioco della mascotte»), altrimenti 204; il comando stampa `{"hookSpecificOutput":{"hookEventName":"UserPromptSubmit","additionalContext":"…"}}`.

**Hook SessionStart sincrono**: `POST /hook/context` (lo usa `calamaio hook --context`, timeout del plugin 3 s, del client 1 s) con il payload del hook → 200 `{additionalContext}` se la sessione non ha un avatar né per sessione né per progetto e `source` è `startup`, `resume`, `fork` o assente; altrimenti 204. Stesse restrizioni di `/hook?wait=1` (403 a richieste con `Origin` o `Sec-Fetch-*`). Il comando stampa `{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"Calamaio: questo progetto non ha un avatar; usa la skill avatar …"}}` e annota `export CALAMAIO_SESSION_ID=<id>` in `$CLAUDE_ENV_FILE`; servizio spento o vecchio → nessun output, uscita 0. `SessionEnd` toglie l'assegnazione per sessione.

**Display** in `/api/status`: `board`, `device`, `class`, `fw`, `w`, `h`, `ip`, `since` (ms epoch del collegamento), `remote` (bool), `settings` (oggetto con le chiavi sopra, oppure `null` se il display non le ha mai mandate), `settingsPending` (chiavi chieste e non ancora confermate, `{}` se nessuna), `error` (`null` oppure `{text, at}`: ultimo comando rifiutato o senza `ack` in 10 s; sparisce al comando successivo). M5: `paired` (il servizio ha una chiave), `displayPaired` (`hello.paired`, `null` per i firmware senza M5), `authed`, `pair` = `none` (non abbinato) · `ok` (abbinato e autenticato) · `redo` (abbinamento da rifare, motivo in `pairReason`) · `auth` (autenticazione in corso; senza risposta alla sfida entro 10 s diventa `redo`), `pairing` (`null` oppure `{id, stage, reason?, until}` con `stage` = `begin` · `code` · `confirm` · `done` · `aborted`; il codice non c'è mai), `perms` (riceve le richieste di permesso).

**Autostart** in `/api/status` (M6; `null` se il servizio non lo sa): `supported` (macOS e Linux sì, Windows non ancora), `kind` (`launchd` · `systemd`), `installed` (c'è il file dell'avvio automatico), `file`, `managed` (questo processo l'ha avviato l'avvio automatico: variabile `CALAMAIO_AUTOSTART`), `exe` e `exeMissing` (binario indicato nel file, se non esiste più), `hint` (cosa fare, in italiano). Il servizio non chiede niente a `launchctl`/`systemctl` (si guarda solo il file): `loaded` c'è solo in `calamaio install --status` e `calamaio status`. La Panoramica mostra «Il servizio non parte da solo al login: calamaio install» se `supported` e non `installed`.

**OTA** in `/api/status`: in radice `ota` (`null` se il servizio non fa aggiornamenti) = `{key, pub?, keyError?, jobs: [Ota]}` (`jobs`: per scheda l'aggiornamento in corso o l'ultimo, anche se la scheda è scollegata, per esempio durante il riavvio) e `lastCrash` (Crash più recente di qualunque scheda o `null`). In ogni Display: `status` (la scheda manda `status`), `batt`, `usb` (`null` = non si sa), `cal`, `openPerms` (richieste di permesso aperte mandate a questa scheda), `ota` (Ota o `null`), `log` (Log o `null`), `otaCheck` `{can, why?, warn?}` (si può aggiornare adesso con il pacchetto del servizio; `why` è il motivo in italiano, `warn` per stessa versione o versione più vecchia) e `lastCrash`. **Ota**: `id`, `board`, `stage` (`offerta` · `ricevo` · `verifico` · `scrivo` · `riavvio` · `fatto` · `rifiutato` · `errore` · `annullato`), `active`, `cancelable`, `from`, `to`, `size`, `sha256`, `done`, `total`, `sent` (byte serviti dal PC), `error?`, `started`, `updated`, `ended?`. **Log**: `id`, `follow`, `active`, `started`, `until?`, `lines`, `reason?`, `file?`. **Crash**: `id`, `board`, `received`, i campi del messaggio `crash`, `decoded` (righe di addr2line), `decodedWith`, `decodeNote`.

**Reti Wi-Fi** in ogni Display di `/api/status`: `wifi` = `{networks, at?, pending, last}`: `networks` come in `wifi-list` (`null` se la scheda non l'ha mai mandata: firmware senza più reti), `at` ms epoch dell'ultima `wifi-list`, `pending` (`null` oppure `{id, op: add|forget, ssid, until}`), `last` (`null` oppure `WifiResult`). **WifiResult**: `id`, `op`, `ssid`, `ok`, `error?` (frase in italiano), `reason?` (motivo dell'`ack` negativo, testo della scheda), `timeout?`, `reboot?`, `wasLast?`, `at`. Mai password.

**Giornata di lavoro** in ogni Display di `/api/status`: `workday` = `{supported, known, on, end, at?, pending, last}`: `supported` (il firmware la gestisce: ha mandato `status.workday`, anche `null`, oppure `workStart`/`workEnd` in `settings`), `known` (la scheda ha riferito lo stato su questa connessione), `on` e `end` dall'ultimo `status.workday`, `at` ms epoch del suo arrivo, `pending` (`null` oppure **WorkdayPending** `{id, action, until}`), `last` (`null` oppure **WorkdayResult** `{id, action, ok, error?, reason?, timeout?, at}`: `error` è la frase in italiano, `reason` il motivo della scheda). Gli orari stanno in `settings.workStart`/`settings.workEnd`. In `/debug` (e quindi `calamaio status`) ogni display ha `workday` `{on, end, at}` se la scheda l'ha riferito.

**Temi** in ogni Display di `/api/status`: `themes` = `{supported, have, cur, job}`: `supported` (la scheda ha mandato `themes` su questa connessione autenticata), `have` e `cur` dall'ultimo `themes` (`[]` e `null` altrimenti), `job` (`null` oppure **ThemeJob** `{id, board, theme, name, crc, stage, error?, started, updated}`: l'operazione in corso o l'ultima, anche a scheda scollegata; `theme` = id o `null` per Calamaio, `stage` come nella sezione «Temi»). **Tema** in `/api/themes`: `{id, name, screen, crc, bytes, screens, updated, warnings, problems?}`: `crc` e `bytes` del CTHM compilato, `screens` = byte del blocco per schermata, `updated` ms epoch dell'ultimo salvataggio, `warnings` avvisi del compilatore, `problems` solo per un file nella cartella che non è valido.

**Permesso** in `/api/status` (richieste aperte, le più vecchie prima): `id`, `session`, `name`, `tool`, `arg`, `at`, `sentTo` (schede che l'hanno ricevuta).

**Hook dei permessi**: `POST /hook?wait=1` (lo usa `calamaio hook --wait`) tiene aperta la richiesta HTTP finché la richiesta di permesso si chiude, senza limite di tempo, e risponde `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}` oppure `…{"behavior":"deny","message":"Negato dal display Calamaio"}…`, o 200 vuoto (nessuna decisione: terminale). Quando la macchina a stati vede il permesso risolto nel terminale (vedi «Permessi» sopra) la risposta finisce vuota e il hook esce da solo; anche se il client chiude la connessione la richiesta si chiude con `terminal`. Una risposta del display che arriva dopo si scarta («id inesistente o non più in attesa»). Risponde 403 a richieste con `Origin` o `Sec-Fetch-*`, con un `Host` non locale o senza `Content-Type: application/json` (nessuna pagina web può aprire una richiesta sul display). `POST /hook` senza `wait` non decide mai.

**Pacchetto firmware**: cartella con `bootloader.bin` (0x0), `partitions.bin` (0x8000), `boot_app0.bin` (0xe000), `firmware.bin` (0x10000), layout dell'ESP32-S3 con la tabella partizioni di default di Arduino (otadata a 0xe000, app0 a 0x10000; NVS a 0x9000 non viene toccata). Cartella: `calamaio serve --firmware DIR`, oppure `firmware_dir` in `config.json`, altrimenti `<dati utente>/calamaio/firmware/` (macOS `~/Library/Application Support/calamaio/firmware`, Linux `~/.local/share/calamaio/firmware`, Windows `%LocalAppData%\calamaio\firmware`). In sviluppo: `--firmware firmware/.pio/build/heltec213`; lì `boot_app0.bin` manca e si prende dal framework Arduino di PlatformIO (`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin`, o sotto `PLATFORMIO_CORE_DIR`) e la versione da `firmware/src/core/model.h`. Un `manifest.json` facoltativo (`{"version", "chip", "device", "parts": [{"name", "offset"}]}`) sostituisce versione, chip e offset; senza, la versione si prende da un file `version` accanto a `firmware.bin`, poi da `model.h`.

**Versione e profilo di un'immagine per l'OTA** (`ota.Resolve` in `service/internal/ota/image.go`, usato da `calamaio ota push` e dal pulsante della webapp). Il descrittore dell'app di Arduino non serve (porta la versione dell'IDF, `esp-idf: v4.4.7`). Versione: `--version`, poi la stringa C `calamaio-fw <versione> <profilo>` se il firmware la include, poi `manifest.json`, poi il file `version`, poi `firmware/src/core/model.h` per le build del repository; **la versione scelta deve comparire nell'immagine come stringa C intera** (byte 0, versione, byte 0), altrimenti rifiuto: così una build vecchia con i sorgenti già cambiati non passa per quella nuova (con `calamaio-fw` le altre fonti, `model.h` escluso, devono coincidere). Profilo: `--device`, `calamaio-fw`, `manifest.json` (`device`), nome della cartella `.pio/build/<env>`, il profilo della scheda di destinazione; anche lui deve comparire nell'immagine. Dimensione massima: la partizione app più piccola di `partitions.bin` accanto, altrimenti 0x240000 (2 359 296 byte, `firmware/partitions_gb.csv`). `firmware.elf` accanto si usa (e si archivia) solo se il suo SHA-256 è uguale a `app_elf_sha256` dell'immagine. Il chip si legge dall'intestazione di `firmware.bin`.

## Telefoni (app iPhone)

Spec: `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §3. Codice: `service/internal/phones`, `service/internal/api/phone.go`, `service/internal/api/phones.go`, `service/internal/server/app.go`.

**Porta**: HTTPS su tutte le interfacce, porta `app_port` (predefinita **8767**, variabile `CALAMAIO_APP_PORT`; con `calamaio serve --loopback` solo `127.0.0.1`, come la 8766 dei display). Accesa solo se c'è almeno un telefono abbinato o una finestra di abbinamento aperta. TLS 1.3, solo `http/1.1`. Certificato ECDSA P-256 autofirmato di 10 anni in `<dati>/app-tls/` (`cert.pem`, `key.pem`, 0600, cartella 0700), creato solo se manca del tutto: uno rovinato non si rifà (si cancella la cartella e si riabbinano i telefoni). L'app fissa l'**impronta** = SHA-256 del DER del certificato, 64 cifre esadecimali minuscole. La 8765 non cambia.

**Bonjour**: `_calamaio-app._tcp` sulla porta dei telefoni, TXT `fp=<prime 16 cifre dell'impronta>`, solo mentre la porta è accesa. `--no-mdns` spegne ogni annuncio Bonjour; `--loopback` fa lo stesso (le porte 8766 e 8767 non sono raggiungibili dalla rete).

**Token**: `Authorization: Bearer <64 cifre esadecimali minuscole>` su ogni richiesta tranne `POST /app/pair`. Il servizio tiene solo lo SHA-256 del token in `<dati>/phones.json` (0600): `{"v":1,"phones":[{"id","name","tokenSha256","paired","lastUsed"}]}` (ms; l'ultimo uso si scrive al massimo ogni 10 minuti). Token assente, malformato, sconosciuto o revocato: **401** `{"error":"non autorizzato"}` con `WWW-Authenticate: Bearer`, sempre uguale, prima di guardare la rotta. Con un token valido, ciò che non è nell'elenco chiuso risponde **404** `{"error":"non trovato"}`.

**Elenco chiuso** (`phoneRoutes`): `GET /`, `/index.html`, `/avatars.html`, `/game.html`, `/device.html`, `/editor.html`, `/assets/…`; `GET /app/whoami`; `GET /api/status`, `GET /api/events`; `POST /api/displays/{board}/settings|full-refresh|workday|theme`; `GET /api/crashes`, `GET /api/crashes/{id}`; `POST /api/sessions/{id}/archive|unarchive|avatar`; avatar (`GET /api/avatars`, `POST /api/avatars`, `GET /api/avatars/{id}`, `POST /api/avatars/default`, `GET /api/avatars/{id}/anchors`, `POST /api/projects/avatar`); gioco (`GET|POST /api/game`, `POST /api/game/wear|special`, `GET /api/accessories/{id}`, `POST /api/accessories`); temi (tutte le rotte `/api/themes…`, compresa `POST /api/themes/check`). Le rotte dei Giochi (`/api/gb/…`) le aggiunge il piano C. **Mai dal telefono**: `/hook*`, `/statusline`, `/debug*`, `firmware.html`, `/vendor/`, `/api/firmware*`, abbinamento e disabbinamento della scheda, Wi-Fi della scheda, OTA, backup, registro remoto, `/api/phones*`. Dentro l'elenco la richiesta passa alla stessa mux della 8765 con un segno nel contesto: l'Host non si controlla e vale come «stessa origine» per `postGuard`/`bodyGuard`/`deleteGuard` (il Content-Type resta obbligatorio); l'`Authorization` si toglie prima.

**`POST /app/pair`** `{"code":"<32 cifre>","name":"<nome del telefono>"}` (`application/json`, ≤ 4 KiB, niente campi in più) → 200 `{"token","id","name"}` · 403 `{"error":"codice sbagliato","left":n}` · 409 nessuna finestra (mai aperta, scaduta, già usata o chiusa dopo 5 errori) · 400 · 405 · 415. Con `Authorization: Bearer <token di prima>` valido il telefono abbinato prima si revoca (QR nuovo dopo un cambio di rete: nessun doppione).

**`GET /app/whoami`** → `{"phone":{"id","name"},"pc":{"name","version"},"hosts":["ip",…],"port":8767}`: l'app ci prova il collegamento e tiene gli indirizzi più recenti.

**QR** (`phones.Link`): `calamaio://pair?v=1&h=<ip1,ip2>&p=<porta>&fp=<impronta completa>&c=<codice>&n=<nome PC>`; nome con `%20` per gli spazi.

**Dalla webapp (solo 8765)**: `GET /api/phones` → `{"phones":[{"id","name","paired","lastUsed"}],"pairing":{"link","expires"}|null,"app":{"port","listening","fingerprint","hosts","error"?}}` · `POST /api/phones/pair` `{}` → 200 la stessa vista con la finestra (5 minuti, codice nuovo) · 409 `{error}` (certificato o elenco illeggibili, porta occupata) · `POST /api/phones/pair/cancel` → 200 · `GET /api/phones/pair/qr.svg` → `image/svg+xml` (il QR del link, livello M, `no-store`; 404 senza finestra aperta; la webapp lo chiede con una HEAD e, se risponde 200, lo mostra sopra il link da copiare) · `DELETE /api/phones/{id}` → 204 · 404 · 500 (revocato in memoria, `phones.json` non scritto). La revoca chiude subito le richieste aperte di quel telefono (SSE compresi); la porta si spegne quando non ci sono più telefoni né finestre.

## Controller dal telefono (Bluetooth LE)

Spec: `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §2. Firmware 0.14.0: `hw/blepad` (radio), `core/pad_phone` (report, mantenimento), `core/pad_bonds` (libro e sicurezza). L'app è la **periferica** e annuncia il servizio; la scheda è la **centrale** e si collega come con l'Xbox (un collegamento alla volta).

| Cosa | UUID | Proprietà | Contenuto |
|---|---|---|---|
| Servizio «Calamaio pad» | `526389E4-B789-4225-990D-0465AFB610B2` | annunciato | nome locale «Calamaio» |
| Stato dei tasti | `6A34D990-D046-4834-96B8-0CBDCE4087FA` | notify, lettura **cifrata** | report v1 (8 byte) |
| Informazioni | `9D00F77E-BB83-4667-A37F-6FBDB0F0F324` | lettura **cifrata** | `ver u8 = 1`, `app` versione «x.y.z» (≤ 15 byte) |

**Report v1** (8 byte, little endian):

| Byte | Campo | Tipo | Note |
|---|---|---|---|
| 0 | `ver` | u8 | 1 |
| 1 | `seq` | u8 | cresce a ogni report; solo per il registro |
| 2–3 | `buttons` | u16 | bit di `pad::Button`: A 0, B 1, X 2, Y 3, LB 4, RB 5, View 6, Menu 7, Guide 8, LS 9, RS 10, Share 11, Su 12, Giù 13, Sinistra 14, Destra 15 |
| 4 | `lx` | i8 | −100..100 (oltre: limitato a ±100), 0 se non usata |
| 5 | `ly` | i8 | come `lx` |
| 6 | `flags` | u8 | bit0 = app in primo piano; bit0 = 0 vale «tutto rilasciato»; altri bit ignorati |
| 7 | `rsv` | u8 | 0, ignorato |

**Scarti**: lunghezza diversa da 8 o `ver` diverso da 1 → report scartato (riga seriale), mai interpretato.

**Mantenimento**: l'app manda un report a ogni cambiamento e uno di mantenimento ogni 1 s (ogni 250 ms con un tasto giù). La scheda, senza report per **3 s** o allo scollegamento, considera tutto rilasciato: una tenuta in corso finisce con HoldUp, mai Select né HoldFire (l'orologio della tenuta non va oltre l'ultimo report). Telefono in secondo piano: scollegato 5 s dopo un report con bit0 = 0 senza report di primo piano, o dopo 10 s senza report validi.

**Sicurezza**: la scheda (centrale) si dichiara **DisplayOnly** e chiede LE **Secure Connections con passkey** (MITM): mostra «Codice per il telefono» e 6 cifre, iOS chiede di scriverle. Il collegamento del telefono vale solo **cifrato, autenticato, chiave da 16 byte e SC** (controllato dalla scheda dopo la cifratura); altrimenti si scollega. Un telefono nuovo si prende solo durante la finestra di 2 minuti di Menu › Gioco › Controller › «Associa telefono». Dal telefono non partono mai CONSENTI, Proteggi, Ripristino, l'abbinamento col PC né i comandi Wi-Fi (DECISIONI «App iPhone»).

**Vettori**: `protocol/vectors/pad-report-v1.json`, generati e controllati con `node protocol/vectors/gen-pad-vectors.mjs --check`; li leggono `test_pad_phone` (firmware) e `PadReportTests` (app).
