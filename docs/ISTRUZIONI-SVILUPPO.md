# Calamaio OS — istruzioni per lo sviluppo

Brief per l'agente Claude Code che sviluppa il software vero: firmware delle schede e-ink, servizio sul PC e plugin di Claude Code.

Leggi tutto il file prima di scrivere codice. Se un punto non è chiaro o manca un dato hardware, chiedi all'utente invece di inventarlo.

---

## 1. Cosa stiamo costruendo

Un piccolo display e-ink a batteria, accanto al PC, che mostra lo stato delle sessioni di Claude Code aperte. Spesso sono 3 o 4 in parallelo. Per ogni sessione mostra:

- **stato**: lavora, chiede un permesso, aspetta una risposta, finito, errore, in pausa, compatta il contesto;
- **avanzamento** dei task, cosa sta facendo adesso, contesto occupato, subagenti attivi, esito di test e build, una frase di riepilogo a fine turno.

Una mascotte in pixel art, **la Goccia**, cambia posa in base alla sessione più urgente.

Dai tasti fisici si può:

- consentire o negare i permessi;
- navigare i menu;
- configurare il Wi‑Fi;
- usare alcuni strumenti: timer di concentrazione, agenda, stato di servizi e pull request, QR della rete ospiti, sensore di CO₂.

Il sistema ha tre pezzi:

```
Claude Code (N sessioni)
   │  hook + status line
   ▼
Servizio Calamaio sul PC  ── WebSocket in rete locale ──▶  Display ESP32 (firmware Calamaio OS)
   ▲                                                        │
   └──────────── risposta ai permessi (firmata) ◀───────────┘

Claude Code su un altro computer ── hook http diretti ──▶ Display   (solo stato, mai permessi)
```

Requisiti di fondo:

- **Multipiattaforma**: il servizio deve girare su Windows, macOS e Linux.
- **Niente cloud**: tutto resta nella rete di casa.
- **Sicurezza dei permessi prima di tutto**: vedi la sezione 7.

---

## 2. Materiale di riferimento

L'utente ti fornisce tre cose. Mettile nel repository in `reference/` e non modificarle, salvo il simulatore (vedi 2.2).

| Materiale | Cos'è | Come usarlo |
|---|---|---|
| `calamaio-os-browser/` | Il simulatore funzionante: pagine HTML apribili con doppio clic, un banco di prova per ogni dispositivo | È la **fonte di verità per l'interfaccia** |
| `calamaio-os-progetto/` | Sorgenti della tela di design (`*.dc.html`, `canvas.json`) | Solo consultazione |
| `Calamaio-OS-guida.pdf` | Guida illustrata di 21 pagine: funzioni, schermate, architettura, hardware, collegamenti | Specifica leggibile, con le schermate attese |

### 2.1 Il simulatore è la specifica dell'interfaccia

Il simulatore fa già tutto questo:

- layout per quattro classi di schermo;
- menu, gesti dei tasti, priorità delle sessioni;
- regole di refresh e-ink e animazioni;
- schermate speciali: PC spento, PC non raggiungibile, riposo, avvio, Wi‑Fi con QR;
- strumenti e testi dell'interfaccia in italiano.

Il firmware deve **tradurre** questa logica in C++, non reinventarla. I testi a schermo vanno ripresi identici.

Il codice è in `calamaio-os-browser/assets/calamaio-test.js`. Le funzioni da cercare con `grep "function <nome>"`:

| Area | Funzioni e costanti |
|---|---|
| Dispositivi e stati | `DEVICES`, `STATES`, `CONTROLS`, `HOME_LABEL`, `ANIM_LABEL`, `ICONS` |
| Modello dati | `newModel`, `logEv`, `count`, `addWait`, `isAttn`, `attention`, `urgentIdx`, `pose`, `muted`, `quiet`, `focusOn`, `powerSave` |
| Metriche e testo | `MET`, `T`, `wrapLines`, `wrapT`, `glyph`, `dotsH`, `dotsV`, `bigBar`, `scrollbar`, `hints`, `hintSet` |
| Mascotte | `mascot` (procedurale, punto per punto: si porta in C++ quasi riga per riga), `HW` |
| Schermate | `draw` (smistamento), `homeS`, `homeM`, `homeW`, `homeL`, `detailS`, `detailBig`, `permS`, `permBig`, `listScreen`, `menuList`, `netItems`, `qrScreen`, `connecting`, `scanning`, `oggi`, `storico`, `batteria`, `stato`, `limiti`, `focusScreen`, `guest`, `servizi`, `agenda`, `ambiente`, `riepilogo`, `offline`, `sleepScreen`, `boot`, `banner`, `statusBar`, `viewHeader`, `drawToast` |
| Pannello | `class Screen` (`compose`, `partial`, `quick`, `full`, `paint`), `animRate`, `frameFor`, `profileText` |
| Comandi (nel componente) | `keyDown`, `keyUp`, `gesture`, `button`, `act`, `confirmPerm`, `activate`, `doAct`, `onTap`, `tick`, `slowTick`, `step` |

Per vedere una schermata, apri `test-*.html` nel browser e premi i tasti. Per catturare schermate in automatico, `DC.instances[0]` dà accesso al componente: `m` è il modello, `scr` lo schermo, `act('ok')` simula un comando.

### 2.2 Tieni il simulatore allineato

Se in fase di sviluppo cambi un comportamento dell'interfaccia, per esempio un testo, un gesto o un layout:

1. aggiorna anche il simulatore in `reference/calamaio-os-browser/`;
2. annota il cambio in `docs/DECISIONI.md`.

---

## 3. Hardware

### 3.1 Schede previste

| Scheda | MCU | Schermo | Classe | Input | Extra |
|---|---|---|---|---|---|
| **Heltec Wireless Paper** (custodia acrilica) | ESP32‑S3 | 2,13″ 250×122 b/n | S | 1 tasto PRG + RST | LoRa SX1262 (non usata) |
| LilyGO T5 V2.4 2,13″ | ESP32 | 250×122 b/n | S | 1 tasto utente IO39 | — |
| **CrowPanel ESP32‑S3 4,2″** (Elecrow) | ESP32‑S3 | 400×300 b/n, SSD1683 | M | rotella su IO6 / giù IO4 / premuta IO5, MENU IO2, EXIT IO1 | microSD, CS IO10 |
| CrowPanel ESP32‑S3 5,79″ | ESP32‑S3 | 792×272 b/n, due SSD1683 (una metà ciascuno) | W | come il 4,2″ | — |
| LilyGO T5 E‑Paper S3 4,7″ | ESP32‑S3 | 960×540, 16 grigi, parallelo | L | 1 tasto IO48, tocco nella versione touch | — |
| Seeed reTerminal E1001 | ESP32‑S3 | 7,5″ 800×480 b/n | L | KEY2 sinistro GPIO5, KEY1 centrale GPIO4, KEY0 destro (verde) GPIO3 | cicalino GPIO45, LED GPIO6, sensore SHT40 su I²C, microSD |

**In grassetto le due schede che l'utente ha già o sta per ricevere.** Si parte da quelle (vedi le milestone nella sezione 9).

### 3.2 Regole sull'hardware

- **Non inventare pin, controller del pannello o partitori della batteria.** I pin in tabella vengono da ricerche fatte in fase di design. Prima di usarli, verificali sullo schema o sul wiki ufficiale della revisione esatta della scheda. Se non trovi un dato, lascia un `TODO(hw)` e chiedi all'utente.
- **Prima del primo flash** di ogni scheda, fai un backup del firmware di fabbrica con `esptool.py read_flash` e dillo all'utente.
- **Controller del pannello** da verificare per ogni scheda:
  - *Heltec Wireless Paper*: esistono revisioni con pannelli diversi; la libreria comunitaria `heltec-eink-modules` le distingue.
  - *CrowPanel 4,2″*: SSD1683, probabilmente compatibile con una classe GxEPD2 400×300 SSD1683.
  - *CrowPanel 5,79″*: due controller, verifica se GxEPD2 ha una classe dedicata.
  - *T5 4,7″*: pannello parallelo, si usa epdiy o la libreria LilyGO.
  - *reTerminal*: usa gli esempi Seeed per la classe del pannello.
- **Risveglio dal sonno profondo**:
  - Sull'ESP32‑S3 solo i GPIO 0–21 sono RTC e possono svegliare la scheda. **IO48 del T5 4,7″ non può svegliarla**: serve un'altra sorgente, da decidere con l'utente.
  - Sull'ESP32 classico i GPIO RTC sono 0, 2, 4, 12–15, 25–27 e 32–39 (IO39 va bene).
  - Il tasto PRG della Heltec di solito è GPIO0: verificalo.
- **Batteria**: LiPo 1S 3,7 V con protezione. Prima di collegarla controlla la polarità del connettore: i JST non hanno un verso standard tra produttori.
- **Accessori opzionali**:
  - sensore CO₂ **SCD40** su I²C (indirizzo 0x62, misura in basso consumo ogni 30 s);
  - buzzer esterno su un pin libero: piezo passivo con resistenza in serie, oppure buzzer magnetico pilotato da un NPN con diodo in parallelo;
  - secondo tasto tra un pin RTC libero e GND, con pull‑up interno.
  - Gli schemi sono a pagina 19 della guida.

### 3.3 Classi di schermo e profili (dal simulatore)

| Scheda | Classe | Agg./s (stima) | Completo ogni N parziali | Profilo animazione |
|---|---|---|---|---|
| Heltec, T5 2,13″ | S | 2 | 40 | ridotta (2 fotogrammi) |
| CrowPanel 4,2″ | M | 2 | 60 | ridotta |
| CrowPanel 5,79″ | W | 2 | 60 | ridotta |
| T5 4,7″ | L, grigi | 4 | 60 | fluida (4 fotogrammi) |
| reTerminal 7,5″ | L | 1 | 30 | a pose |

**Sono stime.** Una delle prime cose da fare su ogni scheda vera è misurare il tempo di refresh parziale e completo. Poi si aggiornano `fps` e `fullEvery` nel file della scheda e nel simulatore (`DEVICES`).

---

## 4. Struttura del repository

Proposta: un monorepo.

```
calamaio/
  README.md
  CLAUDE.md                 ← breve: rimanda a questo file e a docs/
  docs/
    ISTRUZIONI-SVILUPPO.md  ← questo file
    DECISIONI.md            ← ogni scelta e deviazione dalla guida
    PROTOCOLLO.md           ← messaggi tra servizio e display (fonte unica)
    HARDWARE.md             ← pin verificati, misure di refresh e consumi
  reference/                ← simulatore, progetto di design, guida PDF (sola lettura)
  protocol/
    schema/*.json           ← JSON Schema dei messaggi
    samples/                ← payload veri catturati da hook e status line
  firmware/                 ← PlatformIO, un env per scheda
    platformio.ini
    src/core/               ← modello dati, stati, priorità, gesti, menu (logica pura, testabile)
    src/ui/                 ← schermate, porting di draw/home*/detail*/…
    src/gfx/                ← buffer, primitive, font bitmap, mascotte, icone, QR
    src/net/                ← Wi‑Fi, provisioning, mDNS, WebSocket, server HTTP /hook e /setup, OTA, SNTP
    src/power/              ← stati di alimentazione, sonno, batteria
    src/boards/             ← heltec213.h, t5213.h, crow42.h, crow579.h, t547.h, reterm75.h
    test/                   ← test nativi (env native) di core/
  service/                  ← servizio per il PC
  plugin/                   ← plugin di Claude Code
  tools/                    ← cattura payload, conversione font, confronto schermate
```

---

## 5. Servizio sul PC

### 5.1 Tecnologia

Linguaggio consigliato: **Go**. Il motivo è un unico binario per Windows, macOS e Linux, senza runtime da installare. Serve anche perché i hook di Claude Code lo chiamano direttamente: non si può contare su Node, perché Claude Code oggi si installa anche come binario nativo.

Se l'utente preferisce TypeScript, l'alternativa è Bun con `bun build --compile`. **Chiedi prima di iniziare M3** e registra la scelta in `DECISIONI.md`.

Librerie indicative, da valutare:

- WebSocket: `github.com/coder/websocket`;
- mDNS: `github.com/grandcat/zeroconf`;
- avvio automatico: `github.com/kardianos/service`;
- ICS: `github.com/arran4/golang-ical`, con `github.com/teambition/rrule-go` per gli eventi ricorrenti.

### 5.2 Un binario, più comandi

```
calamaio serve                 servizio in background (avviato al login)
calamaio hook [--wait]         chiamato dagli hook di Claude Code: legge il JSON da stdin e lo inoltra al servizio
calamaio statusline            usato come status line: inoltra il JSON e stampa una riga
calamaio pair                  abbina un display: chiede il codice a 6 cifre mostrato sullo schermo
calamaio setup claude          configura la status line in ~/.claude/settings.json (vedi 6.3)
calamaio status                diagnostica: sessioni, display collegati, ultimi eventi
calamaio install / uninstall   avvio automatico come servizio utente
```

### 5.3 Porte e interfacce

- **API locale** solo su `127.0.0.1` (porta proposta 8765):
  - `POST /hook` riceve gli eventi dagli hook;
  - `POST /statusline` riceve il JSON della status line;
  - `GET /debug` mostra lo stato.
- **WebSocket per i display** sull'interfaccia di rete locale (porta proposta 8766), percorso `/ws`.
- **Annuncio mDNS** con il servizio `_calamaio._tcp` e TXT `proto=1`. Il display lo trova da solo.
  - Nel mockup compare `calamaio.local` come nome del PC. Va bene come alias se si riesce a pubblicarlo, ma il display deve funzionare anche solo con la scoperta del servizio.
- **Configurazione** in `os.UserConfigDir()/calamaio/config.json`:
  - porte;
  - link ICS;
  - siti da controllare;
  - repository per le PR;
  - opzione per nascondere comandi e riepiloghi;
  - display abbinati e loro chiavi.
- **Dati** (statistiche del giorno, storico) nella cartella dati dell'utente.

### 5.4 Da evento a stato della sessione

Una sessione si identifica con `session_id`.

**Nome della sessione**: si usa `session_name` della status line se c'è, altrimenti il nome della cartella `cwd`. Se due sessioni hanno lo stesso nome, si aggiunge un suffisso breve.

| Evento | Effetto |
|---|---|
| `SessionStart` | crea la sessione in `idle` |
| `UserPromptSubmit` | `working`; azzera il riepilogo; registra «nuovo prompt» |
| `PreToolUse` | `working`; `detail` = strumento + argomento corto, per esempio `Edit src/app.ts` o `Bash: npm run test` |
| `PermissionRequest` | `permission`; `attnAt` = adesso; parte il flusso della sezione 7 |
| `Notification`, tipo `permission_prompt` | `permission`, se non è già in corso dal hook PermissionRequest |
| `Notification`, tipo `idle_prompt` o `elicitation_dialog` | `input`; `attnAt` = adesso |
| `PostToolUse` | torna `working` se era `permission`; se il comando Bash è un test o una build, aggiorna `test` (`ok`, `ko` o `run`) |
| `SubagentStart` / `SubagentStop` | `subs` ± 1, contando per `agent_id` |
| `TaskCreated` / `TaskCompleted` | lista `tasks` e avanzamento |
| `PreCompact` / `PostCompact` | `compact`, poi torna allo stato precedente |
| `Stop` | `done`; `summary` = prima frase di `last_assistant_message`, senza markdown, al massimo circa 120 caratteri |
| `StopFailure` | `error`, con il motivo: `rate_limit`, `overloaded`, `authentication_failed`, `billing_error` |
| `SessionEnd` | rimuove la sessione dopo un breve ritardo |

Altre regole:

- **Priorità per scegliere la sessione più urgente**, come in `STATES` del simulatore: permission 6, input 5, error 4, working e compact 3, done 2, idle 1.
- **Test e build**: riconosci i comandi con un'espressione regolare configurabile, per esempio `npm|pnpm|yarn (run )?(test|build)`, `pytest`, `cargo (test|build)`, `go test`, `vitest`, `jest`, `make (test|build)`, `tsc`. L'esito viene dal risultato del comando nel payload di `PostToolUse`.
- **Avanzamento dei task**: i payload esatti di `TaskCreated` e `TaskCompleted` vanno verificati (vedi 6.4). Se i task arrivano anche come input di uno strumento (`PreToolUse` di uno strumento di lista dei task), supporta entrambe le fonti.
- **Dalla status line**, per ogni sessione:
  - `context_window.used_percentage` → `ctx`;
  - `cost.total_cost_usd`, `cost.total_lines_added`, `cost.total_lines_removed` → statistiche di Oggi.
- **Limiti**: `rate_limits.five_hour` e `rate_limits.seven_day` (`used_percentage`, `resets_at` in secondi epoch) sono dell'account e valgono per tutto il display.
  - Ci sono solo con i piani Pro e Max e dopo la prima risposta della sessione.
  - Ogni finestra può mancare: gestisci l'assenza.
- **Oggi**: le statistiche si azzerano a mezzanotte, ora locale.
  - task completati;
  - permessi risolti dal display e dal terminale;
  - attesa media (da `attnAt` alla risoluzione);
  - righe aggiunte e tolte;
  - costo. È stimato a prezzo di listino, quindi va sempre scritto «stimato».
- **Strumenti del servizio**:
  - agenda dai link ICS, letta ogni 15 minuti, prossime 24 ore;
  - siti controllati ogni 60 secondi, con timeout di 5 secondi;
  - pull request con `gh` e output `--json`, ogni 5 minuti. Verifica i campi con `gh pr list --help`; `gh` deve essere già autenticato.

### 5.5 Spegnimento del PC

Quando il servizio si ferma in modo pulito (logout, spegnimento, arresto del servizio), manda `bye` a tutti i display. Il display mostra il riepilogo «PC spento» e va in sonno profondo.

Se invece la connessione cade senza `bye`, il display mostra «PC non raggiungibile» e riprova.

---

## 6. Plugin di Claude Code

### 6.1 Documentazione da leggere prima

- Hook: <https://code.claude.com/docs/en/hooks>
- Status line: <https://code.claude.com/docs/en/statusline>
- Plugin (manifest e struttura): <https://code.claude.com/docs/en/plugins-reference>

La documentazione cambia spesso: in caso di dubbio vale quella, non questo file.

### 6.2 Struttura

```
plugin/
  .claude-plugin/plugin.json      name: "calamaio" (kebab-case, senza "claude" nel nome)
  hooks/hooks.json
  skills/setup/SKILL.md           guida l'utente a installare il servizio e configurare la status line
```

Il file `hooks/hooks.json` usa la forma **exec** (`command` + `args`), così funziona anche su Windows senza shell:

- tutti gli eventi della tabella 5.4 chiamano `calamaio` con `args: ["hook"]` e `"async": true`, così non rallentano Claude Code;
- `PermissionRequest` chiama `args: ["hook", "--wait"]`, **non** async, con `"timeout": 25`.

`calamaio hook`:

- legge lo stdin;
- fa POST a `127.0.0.1:8765/hook` con un timeout breve;
- se il servizio non risponde, **esce con 0 e senza output**. Non deve mai disturbare Claude Code.

`calamaio hook --wait` (solo per PermissionRequest):

- aspetta la risposta del display per un massimo di 20 secondi;
- poi stampa la decisione (formato in 6.4) oppure niente.
- Senza output Claude Code prosegue con il normale flusso di permesso nel terminale.

Alternativa possibile: hook di tipo `http` verso `127.0.0.1`. Per le risposte non 2xx Claude Code segnala errori non bloccanti, quindi va provata prima di sceglierla.

### 6.3 Status line

**Un plugin non può impostare la status line**: nelle impostazioni di plugin hanno effetto solo `agent` e `subagentStatusLine`. Quindi `calamaio setup claude`, guidato dalla skill del plugin:

1. chiede il consenso all'utente;
2. fa un backup di `~/.claude/settings.json`;
3. imposta `statusLine` come `{ "type": "command", "command": "calamaio statusline" }`;
4. salva in config il comando di status line che c'era prima.

`calamaio statusline`:

- legge lo stdin e lo inoltra al servizio, senza aspettare più di circa 150 ms;
- poi esegue la status line precedente passandole lo stesso stdin e ne stampa l'output;
- se non ce n'era una, stampa una riga sobria: nome sessione, contesto, 5h.

Claude Code aggiorna la status line agli eventi, con un debounce di 300 ms, e cancella lo script se ne arriva uno nuovo. Deve essere velocissimo.

### 6.4 Payload: prima catturali, poi scrivi il parser

**Primo compito su servizio e plugin (milestone M0).** Costruisci una modalità `calamaio serve --capture` che salva in `protocol/samples/` ogni payload ricevuto da hook e status line, uno per riga JSONL, con l'evento e l'ora.

Poi fai lavorare l'utente qualche minuto con Claude Code e una sessione vera:

- un permesso concesso e uno negato;
- una domanda;
- un subagente;
- dei task;
- una compattazione;
- dei test che falliscono.

Scrivi il parser **solo** su payload veri. Usa quei file come fixture dei test.

Fatti già verificati nella documentazione, da ricontrollare comunque:

- **Campi comuni a tutti gli eventi**: `session_id`, `transcript_path`, `cwd`, `permission_mode`, `hook_event_name`. In più `agent_id` e `agent_type` quando l'evento viene da un subagente.
- **`PreToolUse`**: `tool_name`, `tool_input` (per Bash: `command`, `description`), `tool_use_id`.
- **`Stop`**: `last_assistant_message`.
- **`Notification`**: i tipi includono `permission_prompt`, `idle_prompt`, `elicitation_dialog`, `auth_success`.
- **`StopFailure`**: i valori di matcher includono `rate_limit`, `overloaded`, `authentication_failed`, `billing_error`.
- **`SessionStart`**: `source` (`startup`, `resume`, `clear`, `compact`, `fork`).
- **Risposta a PermissionRequest**:
  ```json
  { "hookSpecificOutput": { "hookEventName": "PermissionRequest",
      "decision": { "behavior": "allow" } } }
  ```
  - `behavior` vale `allow` o `deny`.
  - Il codice di uscita 2 qui non vale: si nega solo con l'oggetto `decision`.
  - Exit 0 senza output vuol dire «nessuna decisione» e prosegue il flusso normale.
  - Verifica se il diniego accetta un campo `message` da mostrare a Claude e usalo («Negato dal display Calamaio»).
- **Status line**:
  - `session_id`;
  - `session_name`, che può mancare;
  - `workspace.current_dir`;
  - `context_window.used_percentage`, che può essere `null` all'inizio;
  - `cost.total_cost_usd`, `cost.total_lines_added`, `cost.total_lines_removed`;
  - `rate_limits.five_hour` e `rate_limits.seven_day`, ciascuno con `used_percentage` e `resets_at`.

---

## 7. Permessi dal display: regole non negoziabili

1. **NEGA è sempre preselezionato.** Per consentire servono due gesti: scegliere CONSENTI, poi confermare.
2. **Il servizio aspetta 20 secondi** (configurabile, mai più del timeout del hook meno un margine). Poi `calamaio hook --wait` esce senza output e la domanda resta nel terminale.
3. **Il display riceve:** id, sessione, strumento e argomento corto. Risponde con `perm-reply`: id, decisione, nonce, HMAC.
   Il servizio accetta la risposta solo se:
   - viene da un display abbinato;
   - l'HMAC è valido;
   - l'id esiste ed è ancora in attesa;
   - il nonce non è già stato usato.
   Altrimenti la scarta e la registra nel log.
4. **Le domande di Claude** (`input`) **non si rispondono mai dal display**: solo nel terminale.
5. **Gli hook diretti** (http verso la scheda) servono solo a mostrare lo stato. Il display **non** risponde mai a un PermissionRequest arrivato per quella via: restituisce 200 con corpo vuoto.
6. **Si spegne** da Menu › Avvisi › Permessi dal display. Quando è spento il servizio non manda proprio le richieste.
7. **Nessuna approvazione automatica**, in nessun caso e per nessuno strumento. Non aggiungere scorciatoie o liste di comandi «sicuri» senza una richiesta esplicita dell'utente.
8. **Abbinamento**:
   - il display mostra un codice di 6 cifre;
   - `calamaio pair` lo chiede;
   - da codice e nonce si ricava una chiave condivisa (per esempio HKDF), salvata nell'NVS del display e nella config del servizio;
   - all'avvio della connessione WebSocket c'è uno scambio sfida-risposta.

Test di accettazione dei permessi, da fare con l'utente:

- consenti dal display;
- nega dal display;
- lascia scadere i 20 secondi;
- annulla con il gesto lungo;
- rispondi dal terminale mentre il display aspetta, e il display deve chiudere la richiesta;
- display spento o fuori rete;
- risposta falsa da un altro client in rete, che deve essere rifiutata.

---

## 8. Firmware

### 8.1 Base

- **Ambiente**: PlatformIO, framework Arduino (core ESP32 3.x), un `env` per scheda e un `env:native` per i test della logica.
- **Librerie indicative**, da verificare la compatibilità:

  | Area | Libreria |
  |---|---|
  | Pannello | GxEPD2 per i pannelli SPI; epdiy o la libreria LilyGO per il T5 4,7″ |
  | JSON | ArduinoJson 7 |
  | WebSocket | `links2004/arduinoWebSockets` |
  | Portale Wi‑Fi | `WiFiManager`, oppure portale proprio con `DNSServer` + `WebServer` |
  | QR | `ricmoo/QRCode` |
  | Sensori | SCD4x di Sensirion; SHT4x per il reTerminal |
  | Sistema | `Preferences` (NVS), `ESPmDNS`, `Update`/`ArduinoOTA` |

### 8.2 Disegno

- **Un buffer in memoria** per tutto lo schermo: 1 bit per pixel, oppure 4 bit sul T5 4,7″. Va copiato sul pannello con aggiornamento parziale o completo.
- **Porta le primitive** del simulatore: `T` (testo con allineamento e troncamento a `max`), barre, puntinati, icone 7×7 da `ICONS`, `glyph`, `bigBar`, `scrollbar`, `hints`.
- **Porta `mascot()` in C++ così com'è**: disegna punto per punto con `P/I/O` e una scala `s`, quindi non servono bitmap. Ci sono 9 pose da 4 fotogrammi: working, permission, input, done, error, idle, compact, tired, stuffy.
- **Font**:
  - il simulatore usa Chakra Petch per i testi e Silkscreen per le etichette in pixel sugli schermi piccoli;
  - converti le dimensioni che servono in font bitmap GFX (fontconvert) o U8g2;
  - controlla le lettere accentate italiane e i simboli usati: `·`, `›`, `″`, `−`, frecce, `✓`, `✗`;
  - i font sono in `reference/calamaio-os-browser/assets/fonts/`, licenza SIL OFL.
- **Layout**: porta `MET(dev)` e le funzioni `home*/detail*/…` per classe. Tutto il resto (menu, liste, schermate di strumenti) è parametrico sulle metriche.

### 8.3 Refresh, come `class Screen`

- **`partial`**: per animazioni e piccoli cambi. Dopo `fullEvery` parziali si fa un refresh completo.
- **`quick`**: refresh completo rapido quando cambi schermata.
- **`full`**: dal menu Display, e dopo il riepilogo «PC spento».
- **Quando animare**: `animRate` decide in base al profilo del pannello, alla modalità Animazioni (Auto, A raffiche, Continue, Spente), al notturno, alla concentrazione e al risparmio.
  - In Auto: raffiche brevi a ogni cambiamento, animazione continua solo finché qualcuno aspetta.
  - In silenzio: niente animazioni, bip o LED.

### 8.4 Input

- **Lettura**: interrupt + antirimbalzo (circa 30 ms), poi lo stesso riconoscitore di `keyUp`/`gesture`:
  - le pressioni si contano se arrivano entro 380 ms l'una dall'altra;
  - lunga da 700 ms;
  - tenuto da 3000 ms.
- **Mappature**: per un tasto solo, rotella e tre tasti sono quelle di `CONTROLS`, `gesture()` e `button()`. Le tabelle sono anche a pagina 10 della guida.
- **La rotella del CrowPanel** si legge come tre tasti (su, giù, premuta), non come un encoder in quadratura. Verificalo sullo schema.
- **Tocco** (T5 4,7″ touch): usa le aree `hit` che le schermate registrano, come `onTap`.

### 8.5 Rete

**Provisioning**:

- al primo avvio, o dopo «Dimentica rete», la scheda crea l'AP `Calamaio-XXXX`, dove XXXX sono le ultime cifre del MAC, con una password casuale;
- mostra il QR in formato `WIFI:T:WPA;S:Calamaio-XXXX;P:<password>;;`;
- apre un portale captive su 192.168.4.1 per scegliere la rete;
- le credenziali vanno salvate in NVS.

**Nome in rete**:

- la scheda si annuncia come `calamaio-xxxx.local` (servizio `_calamaio-board._tcp`) e cerca `_calamaio._tcp`;
- **correzione rispetto al mockup**: il menu dice «Hook diretti → http://calamaio.local/hook», ma `calamaio.local` è il PC. L'URL corretto è quello della scheda, `http://calamaio-xxxx.local/hook`. Aggiorna testo e simulatore.

**WebSocket**:

- la scheda è il client e si collega al servizio;
- riconnessione: subito, poi ogni 30 s, mostrando «PC non raggiungibile» con l'ora dell'ultimo aggiornamento;
- a ogni connessione riceve lo stato completo.

**Altro**:

- **Server HTTP sulla scheda**: `/hook` per gli hook diretti, con una versione ridotta della macchina a stati della 5.4 (solo stato e dettaglio); `/setup` per il portale; `/ota` solo dal servizio abbinato.
- **Ora**: SNTP con fuso `CET-1CEST,M3.5.0,M10.5.0/3`, oppure l'ora che manda il servizio in `welcome`.

### 8.6 Energia

Tre stati, come a pagina 18 della guida:

| Stato | Quando | Cosa resta acceso |
|---|---|---|
| **Attivo** | una sessione lavora o aspetta, o stai usando i tasti | Wi‑Fi e WebSocket |
| **Attesa** | sessioni ferme | Wi‑Fi in risparmio (modem sleep o light sleep), un aggiornamento al minuto |
| **Sonno profondo** | dopo `bye` o «Riposo» | niente; si sveglia con un tasto (pin RTC) o con un timer ogni 15 minuti per cercare il PC |

- **Batteria**: il livello si legge con l'ADC sul partitore della scheda; pin e rapporto vanno verificati. La percentuale si ricava da una curva LiPo; l'autonomia stimata va in Menu › Batteria.
- **Risparmio automatico** sotto il 15%.
- **Misura i consumi veri** in ogni stato e scrivili in `docs/HARDWARE.md`. Le autonomie nella guida sono stime.

### 8.7 Dati locali

- **Solo della scheda**, salvati in NVS:
  - impostazioni (stile home, animazioni, inverti, ruota, avvisi, notturno, permessi dal display, suono, LED);
  - Wi‑Fi;
  - chiave di abbinamento.
- **Gestiti dalla scheda**:
  - timer di concentrazione;
  - letture dei sensori;
  - storico, su microSD dove c'è, altrimenti gli ultimi eventi in RAM.

---

## 9. Milestone

Ogni milestone finisce con:

- una dimostrazione che l'utente può ripetere sulla scheda, con passi scritti;
- i test che passano;
- un aggiornamento di `DECISIONI.md`.

Prima di passare alla successiva, chiedi conferma.

| # | Obiettivo | Fatto quando |
|---|---|---|
| **M0** | Repository, `CLAUDE.md` breve, schema del protocollo, `calamaio serve --capture` e plugin minimo che inoltra tutto | In `protocol/samples/` ci sono payload veri di tutti gli eventi della 5.4 e della status line |
| **M1** | Firmware «ciao pannello» sulla **Heltec**: avvio, Goccia animata, tasto PRG con i gesti stampati in seriale | Tempi di refresh parziale e completo misurati e annotati |
| **M2** | Interfaccia classe S con stato finto incluso nel firmware: le 4 Home, Dettaglio, Permesso (senza rete), Menu, Stato, Riposo | Le schermate corrispondono a quelle del simulatore (stesse posizioni e testi) |
| **M3** | Provisioning Wi‑Fi con QR, mDNS, WebSocket; servizio che manda lo stato finto | La scheda si configura dal telefono e mostra lo stato mandato dal PC |
| **M4** | Servizio con dati veri (hook, status line, macchina a stati), installazione del plugin, `setup claude` | Aprendo sessioni vere le righe compaiono e cambiano stato correttamente |
| **M5** | Permessi end‑to‑end con abbinamento, HMAC e timeout | Tutti i test della sezione 7 superati |
| **M6** | Energia: stati, sonno, batteria, `bye` e riepilogo «PC spento», riconnessione | Consumi misurati; la scheda sopravvive a una notte e al riavvio del PC |
| **M7** | **CrowPanel 4,2″** (classe M, rotella e tasti), poi 5,79″, reTerminal, T5 4,7″, T5 2,13″ | Ogni scheda ha il suo file in `boards/` e le misure in `HARDWARE.md` |
| **M8** | Strumenti e accessori: Limiti, Oggi, Concentrazione, Agenda, Servizi e PR, Wi‑Fi ospiti, Ambiente, buzzer, LED, SCD40, secondo tasto, hook diretti | Funzionano come nel simulatore |
| **M9** | Distribuzione: installer del servizio per i tre sistemi, pagina di flash da browser (ESP Web Tools), aggiornamenti OTA | Un'installazione pulita su un PC nuovo segue i 7 passi di pagina 20 della guida |

---

## 10. Come lavorare

- **Lingua**:
  - testi dell'interfaccia in italiano, identici al simulatore;
  - identificatori nel codice in inglese;
  - commenti brevi in italiano;
  - documentazione in `docs/` in italiano.
- **Test**:
  - la logica pura del firmware (`src/core`: stati, priorità, gesti, menu, `animRate`) si testa con `pio test -e native`;
  - il servizio si testa con le fixture catturate in M0;
  - opzionale e utile: una modalità del simulatore che si collega al servizio vero, per vedere i dati reali senza scheda.
- **Prima di cambiare file dell'utente** fuori dal repository (`~/.claude/settings.json`, avvio automatico, firewall), chiedi e fai un backup.
- **Prima di operazioni che non si annullano** chiedi conferma: flash che cancella il firmware di fabbrica, cancellazione della NVS, rotazione delle chiavi di abbinamento.
- **Commit piccoli e descrittivi.** Non pubblicare niente (repository pubblici, release) senza chiedere.
- **Quando un dato hardware manca** o la documentazione di Claude Code contraddice questo file, fermati su quel punto, scrivi cosa hai trovato e chiedi. Intanto vai avanti sul resto.

---

## 11. Già deciso nel design

Riepilogo rapido. I dettagli e le schermate sono nella guida PDF e nel simulatore.

- **Classi di schermo**:
  - S 250×122: niente barra di stato, icone d'allarme solo se serve, pagina Stato nel menu, 4 stili di Home;
  - M 400×300: barra in alto, Goccia a sinistra, quattro righe;
  - W 792×272: una colonna per sessione;
  - L 800×480 / 960×540: in più ultimi eventi e prossimo appuntamento.
- **Barra di stato**: «5h NN%», PC collegato, tacche Wi‑Fi, batteria con fulmine in carica, ora. In più, dove ci sono, temperatura e umidità, CO₂ e clessidra della concentrazione.
- **Menu**:

  | Voce | Contenuto |
  |---|---|
  | Limiti | |
  | Strumenti | Concentrazione, Agenda, Servizi e PR, Wi‑Fi ospiti, Ambiente |
  | Wi‑Fi | Rete, Segnale, Indirizzo, Cerca reti, Configura da telefono, Server PC, Hook diretti, Dimentica rete |
  | Display | Stile home, Animazioni, Inverti colori, Ruota 180°, Refresh completo, Profilo pannello |
  | Avvisi | Silenzia, Notturno 22–8, Permessi dal display, Cicalino o buzzer, LED |
  | Oggi | |
  | Storico | |
  | Batteria | |
  | Stato | solo S |
  | Riposo | |
  | Info | Sistema, Accessori, Dispositivo, Schermo, Refresh, Comandi, Indirizzo, Acceso da, Riavvia |

- **Gesti con un tasto solo**:

  | Gesto | Effetto |
  |---|---|
  | 1 pressione | avanti |
  | 2 pressioni | apri o conferma |
  | 3 pressioni | menu, o Home dal menu |
  | Lunga | indietro; nella Home silenzia per 1 ora |
  | Tieni 3 s | riposo |
  | Nel permesso | 1 cambia, 2 conferma, lunga annulla |

- **Silenzio**: con Silenzia (1 ora o fino alle 8), Notturno 22–8 e Concentrazione (25 minuti) non ci sono animazioni, bip né LED, ma lo stato si aggiorna lo stesso.
- **Pose speciali**:
  - STANCA quando il limite delle 5 ore supera il 90%, al posto di Lavora, Finito e In pausa;
  - ARIA VIZIATA quando la CO₂ supera 1000 ppm e nessuna sessione aspetta.
- **Oggi** su M: 6 riquadri su 2 colonne. Se c'è l'avviso in basso, la griglia si accorcia per non finirci sotto (corretto nel simulatore).

Buon lavoro. Il primo messaggio all'utente dovrebbe proporre il piano di M0 e M1 e chiedere le due cose che bloccano:

1. la conferma su Go per il servizio;
2. la revisione esatta della Heltec, scritta sulla scheda o sulla scatola.
