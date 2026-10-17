# Fixture del servizio (M4)

Payload **veri** di Claude Code 2.1.296, ripuliti, usati da tutti i test di `service/` (macchina a stati in `internal/sessions`, schema dei messaggi, servizio intero).

| File | Cosa contiene |
|---|---|
| `cattura-2026-10-11.jsonl` | 101 righe (`ts`, `source` hook/statusline, `event`, `payload`) da `calamaio serve --capture`: 2 sessioni, 75 hook e 26 status line. |
| `transcript-negato.jsonl` | coda del transcript della sessione A al momento del permesso **negato nel terminale** (nessun hook lo segnala: vedi sotto), ridotta ai campi che legge il servizio. |
| `stopfailure-sintetico.jsonl` | **sintetico** (`"synthetic": true` in ogni riga): `StopFailure` costruito dall'esempio della documentazione degli hook; un errore API vero non si provoca a comando. |

## Rigenerarle

La cattura grezza (`protocol/samples/capture-*.jsonl`) e i transcript restano fuori da git: contengono prompt e percorsi reali.

```
cd service
go run ./cmd/mkfixtures \
  -capture ../protocol/samples/capture-2026-10-11.jsonl -lines 101 \
  -transcript ~/.claude/projects/<cartella del progetto di prova>/<session_id della sessione A>.jsonl \
  -out ../protocol/samples/fixtures
```

`-lines 101`: le fixture sono le prime 101 righe; il servizio in cattura può averne aggiunte altre allo stesso file. Stesso ingresso, stessi file (controllato con md5). La pulizia: percorsi → `/home/utente/progetto-prova`, `transcript_path`, `agent_transcript_path`, `scratchpad_dir` e `outputFile` finti, esito di `ToolSearch` svuotato (elencava i server MCP dell'utente), testi oltre 240 caratteri accorciati a 200 con «…[accorciato]», nel transcript solo blocchi `text`, `tool_use` e `tool_result` (niente `thinking`). Se resta una parola vietata (nome utente, `/Users/`, `/private/`, …) il comando si ferma e non scrive niente; `TestFixturesHaveNoPersonalData` ricontrolla.

## La storia (per leggere i test)

**Sessione A** `dcbb543b…` (righe 0–65): crea `index.html` (Write, permesso **concesso** dopo 30 s), prova `style.css` (permesso **negato**: dopo la notifica arriva solo una status line e poi, 11 s dopo, il prompt successivo), subagente Explore in background (SubagentStart/Stop, i suoi Bash arrivano con `agent_id` mentre un permesso dell'agente principale è aperto), `test.js` e `node test.js` (permesso concesso, **PostToolUseFailure** con `Exit code 1`), prompt di sistema `<task-notification>`, **Stop**, domanda **AskUserQuestion** (arriva come PermissionRequest e come Notification permission_prompt), Write + Edit concessi, Stop, `/compact` (PreCompact, SessionStart `source: compact`, PostCompact), SessionEnd.

**Sessione B** `89b92428…` (righe 66–100): TaskCreate ×2 (**TaskCreated**), Bash, Read ×2, TaskUpdate completed ×2 (**TaskCompleted**), Stop, **idle_prompt** 60,0 s dopo lo Stop, SessionEnd.

Quattro **SubagentStop senza SubagentStart** (`agent_type` vuoto): sono agenti interni di Claude Code (suggerimento del prompt successivo, riassunto della compattazione) e vanno ignorati.
