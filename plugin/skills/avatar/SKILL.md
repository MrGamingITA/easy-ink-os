---
name: avatar
description: Crea o assegna l'avatar di una sessione Calamaio, il personaggio in pixel art che il display e-ink mostra al posto della Goccia per riconoscere la sessione a colpo d'occhio. Usare quando l'utente chiede un avatar, una mascotte o un personaggio per il progetto o la sessione, vuole cambiarlo, vedere quali sono liberi o cambiare l'avatar predefinito, oppure quando il servizio Calamaio segnala all'avvio che il progetto non ha un avatar.
---

# Avatar di una sessione Calamaio

Specifica completa: `docs/AVATAR.md` del repository Calamaio. Il repository è tre livelli sopra la cartella di questa skill (`<cartella della skill>/../../..`); lì ci sono anche gli avatar predefiniti (`avatars/`) e gli strumenti (`tools/avatar/avatar.mjs`, Node 22). Qui sotto `AV` vuol dire `node <repository>/tools/avatar/avatar.mjs`.

## Comandi del servizio

- `calamaio avatar list [--free] [--json] [--session ID]`: tutti gli avatar con «libero» o «in uso da <sessione>», `(predefinito)` accanto a quello predefinito e `[utente]` accanto a quelli della libreria dell'utente; `--free` solo i liberi da proporre. La sessione «corrente» (che non conta come uso) è `--session`, altrimenti `$CALAMAIO_SESSION_ID`, che il plugin imposta all'avvio della sessione.
- `calamaio avatar add <file> [--replace]`: valida (stesse regole di `AV validate`) e copia nella libreria dell'utente; un id già presente si sostituisce solo con `--replace`; gli id dei predefiniti (goccia, gatto, demone, calamaro) non si possono usare.
- `calamaio avatar assign <id> [--project DIR | --session ID]`: assegna. Senza opzioni vale per il progetto della cartella corrente, cioè la radice git che la contiene (altrimenti la cartella stessa).
- `calamaio avatar unassign [--project DIR | --session ID]`: toglie l'assegnazione (torna il predefinito).
- `calamaio avatar default [<id>]`: mostra o cambia l'avatar predefinito (quello delle sessioni senza avatar e delle schermate senza sessione). Cambialo solo se l'utente lo chiede.
- `calamaio avatar show <id> [--pose POSA]`: anteprima nel terminale.

Con il servizio acceso i comandi aggiornano subito display e webapp (`http://127.0.0.1:8765/avatars.html`); con il servizio spento scrivono libreria e `config.json` e lo dicono. Se `calamaio avatar` risponde «comando sconosciuto», il binario è più vecchio: dillo all'utente, salva comunque l'avatar in `avatars/` del repository e fermati prima di assegnarlo.

Un avatar è **libero** se non è mostrato da nessun'altra sessione aperta e non archiviata. Il predefinito (all'inizio la Goccia) è il ripiego di tutti: non proporlo mai come scelta, e `--free` non lo elenca.

## Flusso di avvio (progetto senza avatar)

Parte quando all'avvio della sessione trovi il contesto «Calamaio: questo progetto non ha un avatar; usa la skill avatar…» (lo manda il hook SessionStart del plugin, con la cartella del progetto), oppure quando l'utente lo chiede.

1. `calamaio avatar list --free --json`.
2. Una sola domanda con AskUserQuestion, «Che avatar vuoi per <progetto> sul display?»: fino a 3 avatar liberi (etichetta = `name`, descrizione = `description`) più «Creane uno nuovo». Se non ce n'è nessuno libero, dillo e offri «Creane uno nuovo» oppure il riuso di uno in uso, avvisando che due sessioni sembreranno uguali sul display.
3. Assegna con `calamaio avatar assign <id> --project "<cartella del progetto>"`, conferma in una riga e **torna subito al lavoro che l'utente ha chiesto**. Se l'utente salta la domanda, resta il predefinito: non chiedere di nuovo nella stessa sessione. Se vuole tenere la Goccia per quel progetto (e non sentirselo più chiedere), `calamaio avatar assign goccia --project …`.

## Creare un avatar nuovo

**Formato in breve.** File `<id>.avatar.json`; griglia 28×25; ogni fotogramma è un array di 25 stringhe di 28 simboli: `.` trasparente, `#` inchiostro, `+` carta (occhi, bocca, interno della nuvoletta). Pose obbligatorie `working permission input done error idle`, facoltative `compact tired stuffy`; 2 o 4 fotogrammi per tutte. Esempio minimo (righe accorciate qui per brevità: nel file sono sempre 25 da 28):

```json
{"format": "calamaio-avatar", "version": 1, "id": "riccio", "name": "Riccio", "author": "Claude",
 "description": "Un riccio con gli aculei a ventaglio.", "w": 28, "h": 25, "frames": 2,
 "poses": {"working": [["....", "…25 righe…"], ["…"]], "permission": [ … ], "input": [ … ],
           "done": [ … ], "error": [ … ], "idle": [ … ]}}
```

**Regole di disegno** (dettagli e misure in `docs/AVATAR.md` §3):
- sagoma piena d'inchiostro di circa 16×16‑20 punti, riconoscibile a scala 1: orecchie, corna, pinne, coda **sul contorno**;
- occhi 4×4 di carta con pupilla 2×2, bocca piccola sotto, separati da almeno una riga d'inchiostro;
- stessa grammatica della Goccia: nuvoletta «…» in working, «!» lampeggiante in permission, «?» in input, due righe in compact; salto con scintille in done; occhi a X, sudore e braccia giù in error; Z in idle; onde in stuffy; sudore in tired;
- angolo x 17..27, y 0..8 libero dal corpo nelle pose con la nuvoletta, anche alzato di 1 punto; 3 righe libere sopra la testa per il salto di done; riga 24 libera per error che scende di 1;
- animazioni piccole (1‑2 punti), niente scacchiere di pixel alterni; il fotogramma 0 deve leggersi da solo e i fotogrammi 0‑1 devono già formare un ciclo.

**Procedimento.**
1. Proponi l'idea in una riga (forma, tratto distintivo, gioco con lo stato) e un `id` kebab-case.
2. Bozza. Il modo più sicuro è un disegno procedurale: copia `tools/avatar/designs/gatto.mjs` in `tools/avatar/designs/<id>.mjs`, cambia sagoma, faccia e braccia (nuvoletta, simboli, Z, scintille vengono da `tools/avatar/lib/kit.mjs`), poi `AV build tools/avatar/designs/<id>.mjs`. In alternativa scrivi direttamente il JSON.
3. `AV validate avatars/<id>.avatar.json`: zero errori; ogni avviso va corretto o motivato.
4. `AV preview avatars/<id>.avatar.json --png <cartella temporanea>/<id>.png` e `AV compare <cartella temporanea>/confronto.png avatars/goccia.avatar.json avatars/<id>.avatar.json`, poi **guarda le immagini con Read**. Chiediti: a scala 1 si capisce cos'è? Le pose si distinguono senza la nuvoletta? Il corpo tocca la nuvoletta o esce dal riquadro? È coerente con la Goccia?
5. Correggi e ripeti 3‑4 finché è bello e leggibile. Mostra il foglio all'utente prima di assegnarlo.
6. Salva: avatar per tutti → `avatars/` del repository (poi `AV library` rigenera le anteprime); avatar personale → `calamaio avatar add <file>` (dalla cartella temporanea: non serve passare dal repository). Poi `calamaio avatar assign <id> --project "<cartella del progetto>"` (o `--session`).

Mai modificare `avatars/goccia.avatar.json` a mano: si genera dal simulatore (`node tools/avatar/goccia-export.mjs`).
