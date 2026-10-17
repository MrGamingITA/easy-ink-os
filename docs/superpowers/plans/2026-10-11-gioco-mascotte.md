# Gioco della mascotte — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gioco opzionale della mascotte (XP, livelli, traguardi, accessori indossabili, accessori speciali creati da Claude e installati senza flash).

**Architecture:** Il servizio Go calcola il gioco dagli eventi delle sessioni (pacchetto puro `internal/game`) e manda alla scheda solo un riassunto (`game`) e i disegni degli accessori (`accessory`, binario CACC). Gli accessori sono dati come gli avatar: JSON validato in Node, impacchettato in CACC, disegnato da una funzione unica `drawAccessory` (firmware e simulatore) sopra la mascotte usando i punti di aggancio dichiarati negli avatar.

**Tech Stack:** Node 22 senza dipendenze (tools/avatar), Go 1.24+ stdlib (service), C++17/Arduino 2.0.17 + Unity (firmware), JS del simulatore.

**Spec:** `docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md`

## Global Constraints

- Gioco **spento di default**; impostazione remota e di menu `game` (bool).
- Accessorio: `w` ≤ 14, `h` ≤ 10, `frames` 1 o 2, simboli `.` `#` `+` `x`, ancore `head-top|eyes|mouth|neck|hand`, binario CACC ≤ 1 KB con CRC-32.
- XP: task +10, turno con riepilogo +20, test rosso→verde +15, sessione chiusa finita/archiviata +10, pausa saggia (≥ 10 min dopo ≥ 90 min di lavoro) +15, primo lavoro del giorno +5 × giorni di serie (max +35); tetto 400/giorno; XP dimezzati dopo 3 h senza pausa saggia.
- **Mai punti** da permessi approvati, costi/token, lavoro notturno, ore filate.
- Livello n→n+1: `round(100 × n^1.3)` XP. Accessorio sbloccato a ogni livello pari. Catalogo iniziale 8: cappellino, occhiali, sciarpa, papillon, corona, cuffie, fiocco, berretto da mago.
- Festa: 3 s, mai con permesso aperto, silenzio, notturno, risparmio energetico (solo toast).
- Testi a schermo in italiano; identificatori in inglese; commenti brevi in italiano. Simulatore prima, poi firmware identico (test_ui).
- Fase 1 tocca solo: `avatars/`, `tools/avatar/`, `docs/AVATAR.md`, `service/internal/game/` (nuovo). Fase 2 dopo la fine dei lavori in corso su firmware/ e service/.

## Review Focus

- Accessorio su un avatar **senza ancore** (avatar utente vecchio): non si disegna, nessun crash — test in Task 3 e Task 7.
- Simbolo `x` sopra pixel **trasparenti** della mascotte (fondo carta): deve diventare inchiostro — test in Task 3.
- Giorno che cambia a mezzanotte durante una sessione lunga (serie e tetto giornaliero) — test in Task 5.
- Eventi duplicati o rigiocati dopo un riavvio del servizio (stesso task completato due volte) non danno XP doppi — test in Task 5.
- Gioco spento: nessun messaggio `game` con accessori, nessuna proposta a Claude, ma progressi conservati se si riaccende — test in Task 6 e Task 9.

---

## FASE 1 (subito, file separati)

### Task 1: Ancore negli avatar

**Files:**
- Modify: `tools/avatar/lib/format.mjs` (o il modulo che oggi valida il JSON avatar), `tools/avatar/avatar.mjs` (nuovo comando `anchors`)
- Modify: `avatars/{goccia,gatto,demone,calamaro}.avatar.json` (campo `anchors`)
- Modify: `docs/AVATAR.md` (§13 ancore)
- Test: `tools/avatar/test/anchors.test.mjs`

**Interfaces:**
- Produces: campo JSON `anchors: { <posa>: [ { "head-top": [x,y]|null, "eyes": …, "mouth": …, "neck": …, "hand": … } per fotogramma ] }`; funzione `getAnchor(avatar, pose, frame, name) → [x,y] | null` esportata da `tools/avatar/lib/anchors.mjs`; blocco CAVT facoltativo in coda (flag bit 0 di un byte di opzioni in intestazione; 5 punti × pose × fotogrammi × 2 byte, 0xFF = assente).

- [ ] Step 1: test che `validate` accetti un avatar con `anchors` coerenti e rifiuti coordinate fuori da 28×25 o un numero di fotogrammi diverso; che `pack`/`unpack` facciano andata e ritorno con e senza ancore e che un CAVT senza ancore resti byte per byte identico a oggi (CRC invariati).
- [ ] Step 2: eseguire `node --test tools/avatar/test/anchors.test.mjs` → FAIL.
- [ ] Step 3: implementare `anchors.mjs`, validazione, pack/unpack; comando `node tools/avatar/avatar.mjs anchors <file> --auto` che propone le ancore: `head-top` = pixel d'inchiostro più alto della colonna centrale della sagoma; `eyes` = centro dei pixel `+` della riga degli occhi; `mouth` = centro della bocca; `neck` = riga sotto la bocca al centro; `hand` = estremità del braccio destro (o `null`).
- [ ] Step 4: applicare `--auto` ai 4 avatar, generare il foglio `avatars/anteprime/ancore-<id>.png` con le ancore marcate, guardarlo e correggere a mano i punti sbagliati; test → PASS; `validate` senza errori.
- [ ] Step 5: commit «Avatar: punti di aggancio per gli accessori».

### Task 2: Formato accessorio, validazione, pack

**Files:**
- Create: `tools/avatar/lib/accessory.mjs`, `tools/avatar/test/accessory.test.mjs`
- Modify: `tools/avatar/avatar.mjs` (sottocomandi `accessory validate|pack|unpack`)
- Modify: `docs/AVATAR.md` (§14 formato accessorio, binario CACC)

**Interfaces:**
- Produces: `validateAccessory(json) → {errors[], warnings[]}`, `packAccessory(json) → Uint8Array` (CACC v1: magia "CACC", versione 1, w, h, frames, ancora (0–4), originX, originY, flags (bit0 outline), poseMask nascoste (u16), n ritocchi, ritocchi [idLen,id,dx,dy], pixel 2 bit per simbolo `.`=0 `#`=1 `+`=2 `x`=3, CRC-32 IEEE dal byte 12), `unpackAccessory(bytes) → json`.

- [ ] Step 1: test: accessorio valido 12×6 a 1 e 2 fotogrammi; errori per w>14, h>10, simbolo ignoto, ancora ignota, origin fuori; pack→unpack identico; CACC ≤ 1024 byte; CRC rovinato rifiutato.
- [ ] Step 2: `node --test tools/avatar/test/accessory.test.mjs` → FAIL.
- [ ] Step 3: implementare.
- [ ] Step 4: test → PASS.
- [ ] Step 5: commit «Accessori: formato JSON e binario CACC».

### Task 3: Disegno di riferimento e controlli visivi

**Files:**
- Create: `tools/avatar/lib/draw-accessory.mjs` (funzione di riferimento usata anche per i golden del firmware), `tools/avatar/test/draw-accessory.test.mjs`
- Modify: `tools/avatar/avatar.mjs` (`accessory preview`, `accessory check`)

**Interfaces:**
- Consumes: `getAnchor` (Task 1), `unpackAccessory`/JSON (Task 2), il disegnatore degli avatar esistente.
- Produces: `drawAccessory(grid, avatar, accessory, pose, frame)` che modifica una griglia 28×25 di `'.'|'#'|'+'` già contenente la mascotte: salta se posa in `hidePoses` o ancora `null`; applica `perAvatar[id]`; disegna il contorno carta (se `outline`) sui pixel trasparenti/carta adiacenti ai pixel dell'accessorio che toccano inchiostro; poi `#`→inchiostro, `+`→carta, `x`→opposto del pixel sottostante (trasparente conta come carta → diventa inchiostro). `checkAccessory(accessory, avatars[]) → problemi[]` con: fuori riquadro, sovrapposizione nuvoletta (x 17..27, y 0..8) nelle pose working/permission/input/compact, contrasto < 60%.

- [ ] Step 1: test con un avatar minimo e accessori sintetici: `x` su inchiostro → carta, `x` su trasparente → inchiostro, contorno, posa nascosta, avatar senza ancore → griglia invariata, ritocco dx/dy, `check` che segnala la nuvoletta.
- [ ] Step 2: → FAIL. Step 3: implementare. Step 4: → PASS.
- [ ] Step 5: commit «Accessori: disegno di riferimento e controlli».

### Task 4: Catalogo iniziale (8 accessori)

**Files:**
- Create: `avatars/accessori/{cappellino,occhiali,sciarpa,papillon,corona,cuffie,fiocco,mago}.accessory.json`, `avatars/anteprime/accessori-*.png`

- [ ] Step 1: disegnare ciascun accessorio (occhiali e cuffie con `x`/`outline` per leggersi sul corpo nero; sciarpa a 2 fotogrammi; occhiali nascosti in error/idle/tired).
- [ ] Step 2: `accessory check` su tutti gli avatar senza problemi; `accessory preview` → foglio accessorio × avatar × pose; GUARDARE ogni foglio e correggere finché bello e leggibile (stessa cura del Demone).
- [ ] Step 3: commit «Accessori: catalogo iniziale di 8».

### Task 5: Motore del gioco (Go, pacchetto puro)

**Files:**
- Create: `service/internal/game/{rules.go,engine.go,achievements.go,state.go}`, `service/internal/game/*_test.go`, `service/internal/game/achievements.json` (embed)

**Interfaces:**
- Produces (usato in fase 2):
  ```go
  type Event struct{ At time.Time; Session string; Kind string /* task_done, turn_done, test_green, session_closed, activity */; Key string /* id univoco per deduplicare */ }
  type State struct{ On bool; XP, Level, XPNext int; Energy int; Streak int; Today string; TodayXP int; LastWork, LastBreak time.Time; Achievements map[string]time.Time; Unlocked []string; Wear string; SpecialPending string; Seen map[string]bool /* chiavi già contate, potate a 30 giorni */ }
  type Engine struct{ /* … */ }
  func New(st State, catalog []string, now func() time.Time) *Engine
  func (e *Engine) Apply(ev Event) (gained int, newAchievements []string, levelUp bool)
  func (e *Engine) Tick(now time.Time) // energia, pause sagge, mezzanotte
  func (e *Engine) State() State
  func (e *Engine) Wear(id string) error // solo sbloccati o ""
  ```
- [ ] Step 1: test, uno per regola: ogni fonte di XP; tetto 400; dimezzamento dopo 3 h senza pausa; pausa saggia; serie e mezzanotte (giorno che cambia durante la sessione); evento duplicato (stessa Key) ignorato; nessun Kind per permessi/costi; formula livelli (livello 2 a 100, 3 a 100+round(100×2^1.3)=346); sblocco a livelli pari in ordine di catalogo; catalogo esaurito + traguardo → SpecialPending; Wear rifiuta accessori non sbloccati.
- [ ] Step 2: `go -C service test ./internal/game/` → FAIL. Step 3: implementare. Step 4: → PASS.
- [ ] Step 5: simulazione `TestCalibration`: rigiocare le fixture di `protocol/samples/fixtures/` scalate a 30 giorni lavorativi tipici e verificare livello 2 entro il primo giorno e livello ~10 a 30 giorni (tollerare 8–12); se no, ritoccare solo i coefficienti dichiarati nella spec e aggiornare la spec.
- [ ] Step 6: commit «Gioco: motore delle regole (puro)».

---

## FASE 2 (dopo la giornata di lavoro: firmware/ e service/ liberi)

### Task 6: Servizio — integrazione

**Files:** `service/internal/game/store.go` (persistenza `<dati>/calamaio/game.json`, scrittura atomica), mappatura eventi in `service/internal/sessions` (TaskCompleted→task_done, Stop con summary→turn_done, test ko→ok→test_green, SessionEnd/archivio da done→session_closed, ogni hook→activity), libreria accessori (predefiniti con go:embed da `avatars/accessori`, utente in `<dati>/calamaio/accessories/`, porting di validate/pack con test di parità con Node come per gli avatar), hub: messaggi `game`, `accessory`, `accessory-need`, `accessory-wear`, impostazione `game`; CLI `calamaio game status|on|off`, `calamaio accessory list|add [--special]|wear|show`; PROTOCOLLO.md, schema, esempi.
- [ ] Test: gioco spento → nessun `game` con `wear`, nessuna proposta, progressi salvati; riavvio del servizio senza XP doppi; `accessory add --special` sblocca e azzera SpecialPending; parità CACC Go↔Node.
- [ ] Commit.

### Task 7: Simulatore e firmware — disegno e schermate

**Files:** simulatore (`drawAccessory`, voce Menu › Display «Gioco della mascotte», voce di menu «La mascotte», festa, pulsanti Simula), firmware (`gfx/accessory.*` identico, cache CACC in LittleFS accanto agli avatar, messaggi, NVS per l'ultimo indossato), `tools/sim-capture` scenari, `test_ui`, golden di `drawAccessory` generati da `tools/avatar/lib/draw-accessory.mjs`.
- [ ] Test: golden pixel per pixel (inverso, contorno, pose nascoste, ritocchi, avatar senza ancore); schermata «La mascotte»; festa bloccata con permesso aperto/silenzio/risparmio; scelta dell'accessorio dal menu manda `accessory-wear`.
- [ ] Commit; versione firmware successiva; installazione via OTA.

### Task 8: Webapp

**Files:** `service/internal/webui/static/` pagina «Gioco» (livello, storico XP, traguardi, galleria con anteprima su ogni avatar, Indossa, interruttore, «Crea accessorio speciale» con istruzioni).
- [ ] Test Playwright con mock; screenshot guardati; nessun errore in console.
- [ ] Commit.

### Task 9: Proposta di Claude e skill `calamaio:accessorio`

**Files:** `plugin/hooks/hooks.json` (UserPromptSubmit sincrono breve → `calamaio hook --context`), servizio: contesto della proposta una volta per traguardo, stati Sì/Più tardi/No via `calamaio game special accept|later|decline`; `plugin/skills/accessorio/SKILL.md` (formato, regole, procedimento con subagente, validate/check/preview da guardare, `calamaio accessory add --special`).
- [ ] Test: proposta una sola volta per traguardo; «più tardi» ripropone solo in un altro giorno; gioco spento → niente; `claude plugin validate plugin`.
- [ ] Prova dal vivo con l'utente: traguardo forzato con catalogo esaurito, Claude propone, accessorio creato e visto sulla scheda senza flash.
- [ ] Commit.
