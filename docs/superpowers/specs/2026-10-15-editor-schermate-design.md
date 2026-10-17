# Editor delle schermate — design

Data: 2026-10-15 · Stato: approvato a voce dall'utente per sezioni (2026-10-15); ramo `editor` (da `giochi`, firmware 0.12.5-gb)

## Scopo

Personalizzare al massimo ciò che mostra la scheda: dalla webapp, con drag & drop e funzioni da editor, si cambiano
disposizione, dimensioni e testi di **tutte le schermate** (tranne Permessi) e i **menu** della scheda. Per esempio:
mascotte più grande, mascotte in alto e testo in basso, testi propri con valori dentro. Le personalizzazioni sono
raccolte in **temi** che si scelgono al volo.

Decisioni dell'utente (2026-10-15):
- approccio A: **schermate come dati**, interpretate dalla scheda e dalla webapp;
- editor «**stile Figma**»: elementi e livelli a sinistra, tela al centro, proprietà a destra (variante A delle bozze);
- **tutte le schermate**: Home, Riposo, Dettaglio sessione e tutte le altre; Permessi resta bloccata;
- contenuti variabili con **riquadro lista + varianti**;
- **più temi**, sceglibili dalla webapp e dalla scheda;
- ordine dei lavori: prima l'editor, poi la pagina Giochi (piano C dei giochi).

## 1. Temi e schermate come dati

- **Tema**: nome (≤ 24 byte), id kebab-case (≤ 24), schermo di destinazione (`250x122`, classe S), versione di formato,
  un **layout per ogni schermata personalizzata** (le altre restano quelle di serie) e la sezione **Menu** (§5).
- Il tema **«Calamaio»** di serie è il codice C++ di oggi: sempre presente, non modificabile, non cancellabile.
- **Layout di una schermata**: varianti con condizione (`sessions == 0`, `sessions == 1`, `sessions >= 2`, `sempre`):
  la scheda usa la prima variante la cui condizione è vera; nessuna vera → schermata di serie.
- **Elementi** (elenco chiuso; ogni elemento ha `x, y, w, h`, `visibleIf` facoltativo, `invert`):
  - `mascot`: scala 1–4 (griglia 28×25), posa `auto` (dallo stato della sessione di riferimento) o fissa, sessione di
    riferimento (`urgente`, `selezionata`, `riga` dentro un riquadro lista, `nessuna` = avatar predefinito), accessorio
    sì/no (come `accOverlay`), animata sì/no;
  - `text`: modello con valori `{…}` (§2), carattere (solo quelli della scheda: Chakra Petch 10–46 nei pesi presenti,
    Silkscreen 8 in 400/700 — l'editor propone solo le combinazioni esistenti in `f_registry.h`), allineamento
    sinistra/centro/destra, righe massime (1 = troncato con «…», >1 = a capo, interlinea), maiuscolo sì/no;
  - pronti: `clock` (formato `HH:MM` o `HH:MM:SS`), `battery` (icona `battIcon` + percentuale facoltativa),
    `wifi` (icona di livello), `stateIcon` (icona dello stato di una sessione), `bar` (fonte: contesto, task, giornata,
    batteria, limiti 5 h/7 g; stile `bar` o `bigBar`), `qr` (testo con valori; QR vero, ECC M, versioni 1–5),
    `sym` (Plug, Bell, Moon, Lock, Bolt, Dot, Glass, Gauge);
  - forme: `line` (piena o a puntini, orizzontale/verticale), `rect` (bordo, riempimento, angoli), `circle`;
  - `list`: riquadro che ripete una **riga tipo** (un gruppo di elementi alto `rowH`) per ogni sessione (in ordine di
    urgenza, come oggi) con spaziatura; se le righe non ci stanno sfoglia ogni N s (impostabile) con indicatore di
    pagina facoltativo; dentro la riga i valori `{riga.*}` puntano alla sessione della riga;
  - `group`: raggruppa elementi (si spostano insieme; nel binario si appiattisce).
- **Condizioni** (`visibleIf`, combinabili con «e»): permessi in attesa, batteria < N %, in carica, notte, PC
  collegato/scollegato, giornata di lavoro attiva, gioco della mascotte acceso, numero di sessioni, stato della sessione
  di riferimento, fotogramma pari/dispari (lampeggio).

## 2. Valori nei testi

Elenco chiuso, documentato nell'editor con un esempio ciascuno. Sessione (`sessione.*` = sessione di riferimento,
`riga.*` dentro un riquadro lista): `nome`, `stato` (testo italiano di `stateText`), `attività` (`lineFor`),
`riassunto`, `meta` (`metaLine`), `contesto` (%), `task` («3/7»), `da` (tempo trascorso, `ago`), `sub`, `test`.
Generali: `ora`, `data`, `giorno`, `sessioni`, `inattesa` (permessi in attesa), `batteria`, `autonomia`, `wifi` (SSID),
`pc` (`pcLabel`), `giornata.mancano`, `giornata.fine`, `oggi.task`, `oggi.righe`, `oggi.costo`, `limiti.5h`,
`limiti.7g`, `mascotte.livello`, `mascotte.xp`, `mascotte.energia`, `co2`, `versione`. Valore mancante → stringa vuota.
Filtri: `|maiuscolo`, `|minuscolo`. Il testo finale passa per la stessa misura/troncatura di oggi (`fitText`,
`wrapLines`).

## 3. Sulla scheda

- **Formato binario `CTHM` v1** (un file per tema): intestazione (magic, versione, lunghezza, CRC-32 come CAVT, id,
  nome, schermo), indice delle schermate, poi per ogni schermata un blocco compatto (elementi a byte, stringhe in una
  tabella). **Limite: 4 096 byte per schermata, 64 KB per tema, 8 temi** sulla scheda.
- **Archivio**: LittleFS `/th/<id>.cthm`; scrittura in `.tmp`, verifica (CRC + interprete in modalità «solo lettura»),
  poi rinomina — come gli avatar. Tema attivo in NVS (`calamaio-th`, chiave `cur`).
- **Interprete** (`core/theme_*`, logica pura provata in native): legge il blocco della schermata corrente in un **buffer
  statico da 4 096 byte** (ricaricato al cambio schermata) e disegna con le funzioni di oggi (`drawText`, `drawAvatar`
  + `accOverlay`, `prims`, `qr`). Nessuna allocazione nello heap durante il disegno. In modalità gioco il buffer non
  serve (la partita ha il suo disegno): si può prestare come l'anello del registro, se servirà.
- **Robustezza**: elemento fuori schermo → tagliato; valore mancante → vuoto; elemento o versione sconosciuti →
  schermata di serie (mai a metà); errore di lettura/CRC → tema ignorato con log e toast «Tema rovinato: uso quello
  di serie».
- **Permessi** (ScreenId::Perm), Pair, Confirm e le schermate di sistema (Boot, Ota, BattLow, Calib, GbLoad, GbPlay)
  restano **sempre di serie**: §7 di ISTRUZIONI-SVILUPPO invariata. Si può cambiare solo l'avatar, come oggi.
- **Uscita di emergenza**: PRG tenuto premuto all'accensione → tema di serie per quell'avvio (toast «Tema di serie»).
- **Scelta del tema**: Menu › Display › Tema (elenco dei temi sulla scheda, ✓ sull'attivo). Il cambio è immediato.

## 4. Sul PC

- **Archivio**: `<dati>/calamaio/themes/<id>.json` (formato leggibile, schema in `protocol/schema/theme.schema.json`)
  + cronologia `<id>/<n>.json` (ultime 20 versioni). Salvataggio automatico dall'editor.
- **Validazione** nel servizio (Go) con le stesse regole dell'interprete: elementi e valori dell'elenco, caratteri
  esistenti, coordinate in ±512, limiti di dimensione del binario; il servizio compila JSON → `CTHM`.
- **API** (stesse guardie di oggi: loopback, JSON, stessa origine): `GET/POST /api/themes`, `GET/PUT/DELETE
  /api/themes/{id}`, `GET /api/themes/{id}/history`, `POST /api/themes/{id}/restore/{n}`, `POST
  /api/themes/{id}/export`, `POST /api/themes/import`, `POST /api/displays/{board}/theme` (`{id}` per inviare e
  attivare, `{id:null}` = tema di serie).
- **Protocollo** (messaggi nuovi facoltativi, PROTOCOLLO.md «Temi»): scheda→servizio `themes {have:[{id,crc}],
  cur}` dopo `hello` e a ogni cambio; servizio→scheda `theme-offer {id, crc, size, token}`; la scheda scarica con
  `GET http://<pc>:8766/themes/<token>` (token monouso, 5 min, legato all'IP della scheda, come l'OTA), verifica e
  risponde `theme-ack {id, ok, error}`; servizio→scheda `theme-use {id|null}`. Solo a scheda **autenticata**. In
  modalità gioco la scheda rimanda (rete spenta): il servizio ripropone al ritorno.
- **Schede diverse**: un tema dichiara lo schermo; si invia solo a schede uguali. Al CrowPanel (M7): «Adatta da
  250×122» crea una copia da sistemare a mano.

## 5. Menu della scheda

Nel tema, sezione `menu`: per ogni menu (`MenuId`) l'ordine delle voci, le voci nascoste, i nomi cambiati (≤ 24
byte) e la lista **Preferiti** (≤ 8 voci di qualunque menu, mostrata in cima al Menu principale). Non si possono
nascondere: Display › Tema, Wi-Fi, Info, Sicurezza e «indietro». Le voci si identificano con una chiave stabile
(nuova, aggiunta a `menu.cpp` per ogni voce: es. `main.limiti`). Voci nuove del firmware non presenti nel tema
compaiono in fondo al loro menu.

## 6. Editor nella webapp (pagina «Editor schermate», variante A)

- In alto: tema (nuovo, duplica, rinomina, elimina, esporta/importa), schermata, variante, annulla/ripeti, dati di prova,
  «Invia alla scheda» (con esito), stato del salvataggio.
- Sinistra: «Aggiungi» (gli elementi di §1) e «Livelli» (ordine con trascinamento, nascondi, blocca, rinomina).
- Centro: lo schermo 250×122 disegnato dal **renderer JS**, zoom ×2–×6, griglia dei pixel; trascina per spostare,
  maniglie per ridimensionare, frecce = 1 px, Maiusc+frecce = 8 px; selezione multipla con riquadro o Maiusc.
- Destra: proprietà dell'elemento scelto; allinea/distribuisci; aggancio a griglia (1/2/4/8 px) e agli altri elementi.
- Funzioni: copia/incolla, duplica, raggruppa/separa, annulla/ripeti illimitati nella sessione, anteprima animata
  (fotogrammi e pose come sulla scheda), **dati di prova** (0/1/6 sessioni, batteria scarica, notte, permesso in
  attesa, PC scollegato), avvisi in tempo reale (testo che non ci sta, elemento fuori schermo o coperto, schermata oltre
  4 096 byte).
- «Parti dalla schermata di serie»: copia nel tema la versione-dati della schermata di serie (§7).
- Sezione **Menu**: elenco delle voci per menu con trascinamento, occhio (nascondi), rinomina, stella (Preferiti).
- Schermata Permessi visibile ma marcata «bloccata (regole di sicurezza)».

## 7. Renderer condiviso e schermate di serie come dati

- **Renderer JS** (`assets/js/theme/`): stesso algoritmo dell'interprete C++; caratteri della scheda esportati da
  `tools/fonts` in un modulo JS (stessi bitmap e crenatura di `f_*.h`), primitive e avatar dalle stesse fonti già
  verificate (`mascot.js`, `accessory.js`). **Test di parità**: per ogni tema di prova e ogni schermata, immagine
  C++ (native) = immagine JS (node) pixel per pixel; gira in `go test` come `accessory_parity_test.go`.
- **Schermate di serie come dati**: per ogni schermata si scrive il tema «Calamaio (copia)» la cui resa è **identica
  pixel per pixel** al C++ negli scenari di cattura esistenti (`tools/sim-capture`, `test_ui`). Ordine: Home (4 stili),
  Riposo, Dettaglio, poi le altre. Dove un dettaglio non si può esprimere con gli elementi, si aggiunge un elemento
  (mai codice nel tema).

## 8. Fasi

1. **Fondamenta**: formato JSON + schema, compilatore Go → `CTHM`, interprete C++ (elementi, valori, condizioni,
   lista, varianti), archivio LittleFS, protocollo e HTTP dei temi, Menu › Display › Tema, uscita di emergenza,
   renderer JS + export dei caratteri + test di parità. Esito: un tema scritto a mano si vede sulla scheda.
2. **Editor** (§6 senza Menu).
3. **Schermate di serie come dati** (§7).
4. **Menu** (§5 e sezione Menu dell'editor).
5. Poi la **pagina Giochi** (piano C dei giochi, spec 2026-10-12).

Ogni fase ha il suo piano in `docs/superpowers/plans/`, eseguito con un agente per passo e revisione.

## 9. Prove

- Automatiche: parità C++/JS; compilatore (temi validi/non validi, limiti, round-trip JSON→CTHM→interprete); interprete
  con temi rovinati, troncati, versioni future (→ di serie, mai crash); valori e condizioni; lista con 0/1/6 sessioni
  e sfoglio; menu (ordine, nascoste, voci non nascondibili, voci nuove in fondo); schermate di serie = C++ al pixel.
- Dal vivo: tema inviato e attivato; cambio dal Menu della scheda; PRG all'accensione; permesso in arrivo con tema
  attivo (schermata di serie); memoria (`[memoria]` con tema attivo e Bluetooth acceso); tema durante e dopo una
  partita.

## Fuori da questa specifica

Immagini/bitmap importate, caratteri nuovi, script o logica nei temi, temi condivisi online, temi per CrowPanel (fino
a M7), animazioni personalizzate oltre a quelle della mascotte.
