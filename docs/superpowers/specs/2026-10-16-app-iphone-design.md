# App «Calamaio» per iPhone e telefono come controller — design

Data: 2026-10-16 · Stato: approvato dall'utente a voce (sezione 1 «Architettura»); sezioni 2–8 decise dal
coordinatore su delega esplicita dell'utente («passa allo sviluppo»), ciascuna scelta segnata **Scelta**.

## 0. Cosa ha chiesto l'utente
- App per iPhone con **tutte** le funzioni della webapp (sessioni, permessi, impostazioni, editor dei temi, Giochi)
  e **ROM e salvataggi** mandati dal telefono come dal PC («ho delle rom anche su smartphone»).
- Il telefono fa da **controller via Bluetooth** per i giochi Game Boy **e per il sistema** (menu, schermate), come
  il controller Xbox.
- Collegamento **misto**: Bluetooth solo per il controller; tutto il resto passa dal servizio sul PC in rete locale.
- Strada **1**: app nativa (Swift/SwiftUI) che contiene la webapp; account Apple **gratuito** (l'app scade dopo 7
  giorni, niente notifiche push); iPhone 17 Pro con **iOS 27**; Xcode 27 già sul Mac.
- Permessi (§7): scelta **A** — il telefono muove menu e giochi, apre un permesso, sceglie e conferma NEGA, ma
  **non può mai dare CONSENTI**: per consentire resta il gesto sulla scheda (PRG) o sul controller Xbox.

Fuori: notifiche push, iPad, Android, uso senza PC (tranne il controller), modifiche al Wi-Fi o all'abbinamento della
scheda dal telefono, OTA dal telefono.

## 1. Architettura
Tre pezzi, nessun protocollo nuovo verso la scheda oltre al controller.

**App iPhone «Calamaio»** (cartella `ios/` del monorepo, SwiftUI, iOS 27). Tre schede:
1. **Scheda** — la webapp del servizio in una `WKWebView`. Tutte le pagine della webapp tranne «Firmware» (Web Serial
   non esiste su iOS).
2. **Controller** — pulsantiera nativa a tutto schermo (verticale e orizzontale) con vibrazione al tocco.
3. **Impostazioni** — PC abbinato, telefono associato alla scheda, preferenze del controller, versione.

**Servizio (Go)** — un secondo ascoltatore **solo per i telefoni abbinati** (§3); stesse API della webapp.

**Firmware** — un secondo tipo di controller in `hw/blepad` (§2); la scheda resta solo **centrale** Bluetooth
(DECISIONI: «già da 0.11.0 solo centrale»).

Flussi: controller = telefono → Bluetooth → scheda. Tutto il resto = telefono → Wi-Fi → servizio → scheda, come la
webapp. Il telefono non parla mai con la scheda in Wi-Fi: nessuna superficie nuova sulla scheda.

## 2. Telefono come controller (Bluetooth LE)
### 2.1 Ruoli e servizio GATT
**Scelta**: l'app è **periferica** (`CBPeripheralManager`) e annuncia un servizio proprio; la scheda, centrale, la
trova e si collega come fa con l'Xbox. Niente ruolo periferica sulla scheda (RAM).

| Cosa | UUID | Proprietà | Contenuto |
|---|---|---|---|
| Servizio «Calamaio pad» | `526389E4-B789-4225-990D-0465AFB610B2` | annunciato | nome locale «Calamaio» |
| Stato dei tasti | `6A34D990-D046-4834-96B8-0CBDCE4087FA` | notify, lettura **cifrata** | report v1 (8 byte) |
| Informazioni | `9D00F77E-BB83-4667-A37F-6FBDB0F0F324` | lettura **cifrata** | `ver u8=1`, `app` versione «x.y.z» (≤ 15 byte) |

Report v1, little endian: `ver u8 = 1`, `seq u8` (cresce a ogni report), `buttons u16` (stessi bit di
`pad::State`: A=1<<0 … Right=1<<15), `lx i8`, `ly i8` (−100..100, 0 se non usati), `flags u8` (bit0 = app in primo
piano), `rsv u8 = 0`. Report di lunghezza o versione diversa: scartato (riga seriale), mai interpretato.

L'app manda un report **a ogni cambiamento** e uno di mantenimento ogni 1 s; la scheda, senza report per 3 s o allo
scollegamento, considera **tutto rilasciato** (stessa regola di oggi: una tenuta in corso finisce con HoldUp, mai
Select né HoldFire). App in secondo piano: iOS ferma l'annuncio; l'app manda prima un report «tutto rilasciato».

### 2.2 Scoperta, associazione, sicurezza
- **Associazione**: Menu › Gioco › Controller › **«Associa telefono»** (nuova voce accanto ad «Associa controller»).
  Finestra di 2 minuti (`kPairMs`). Durante la finestra la scheda accetta un dispositivo **non associato** solo se
  annuncia l'UUID del servizio «Calamaio pad» ed è collegabile.
- **Scelta**: LE Secure Connections **con passkey** per il telefono. La scheda si dichiara *DisplayOnly*, mostra
  sull'e-ink «Codice per il telefono» e 6 cifre; iOS chiede di digitarle. Il collegamento del telefono è accettato
  solo se **cifrato e autenticato** (MITM); altrimenti la scheda scollega e mostra «Associazione non riuscita».
  L'Xbox resta come oggi (Just Works, non autenticato): la combinazione DisplayOnly/NoInputNoOutput ricade in Just
  Works. Da verificare dal vivo che l'Xbox si riassoci (prova obbligatoria).
- **Abbinamenti**: oggi se ne tiene uno solo. **Scelta**: fino a **due**, uno per tipo (un Xbox, un telefono). Il
  tipo di ogni indirizzo associato sta nella NVS `calamaio-gb` (`padx`, `padp`: indirizzo 6 byte + tipo). Associare
  un nuovo telefono sostituisce il telefono vecchio, mai l'Xbox (e viceversa). «Dimentica controller» e «Dimentica
  telefono» separati.
- **Uno alla volta**. **Scelta** (corregge «vince l'ultimo» detto in chat, che costerebbe una ricerca continua a
  radio accesa durante il gioco): la scheda si collega al **primo** dispositivo associato che trova; per passare
  all'altro si spegne/chiude quello collegato. L'app, se la scheda non si collega entro 10 s, mostra «La scheda usa
  un altro controller: spegnilo per usare il telefono».

### 2.3 Cosa può fare il telefono
Lo stato del telefono diventa un `pad::State` con **sorgente = Telefono**; menu, gioco e mappatura (`MenuMapper`)
sono gli stessi dell'Xbox. Unica eccezione, §7 (scelta A dell'utente):
- su una schermata di permesso, un **HoldFire con CONSENTI selezionato** proveniente dal telefono **non consente**:
  la scheda resta sul permesso, toast «Per consentire usa il tasto PRG o il controller». Scegliere CONSENTI con le
  frecce è permesso (si vede), confermarlo no.
- NEGA (preselezionato) si conferma anche dal telefono.
- La regola sta nel codice che risponde ai permessi, non nella mappatura: un test nativo la copre per ogni percorso
  che porta a `perm-reply` con decisione «consenti».
- Le domande di Claude (`input`) restano non rispondibili da scheda (regola 4), quindi nemmeno dal telefono.

DECISIONI: la riga «il telefono comanda solo il gioco, mai i permessi di Claude né i menu» si aggiorna in «il
telefono comanda gioco e menu; mai il consenso a un permesso».

### 2.4 Pulsantiera
Verticale: in alto lo stato del collegamento (non la schermata della scheda: in Bluetooth non viaggiano
dati verso il telefono); croce a sinistra, A/B a destra, Select/Start sotto, tasto Menu. Orizzontale: croce e A/B
ai lati. Mappatura: croce → Up/Down/Left/Right, A → A, B → B, Start → Menu, Select → View, tasto «Guida» → Guide.
Vibrazione leggera a ogni pressione (disattivabile). Multi-tocco (A+B, croce in diagonale).

## 3. Telefono ↔ servizio sul PC
### 3.1 Ascoltatore per i telefoni
- **Scelta**: un ascoltatore **HTTPS** separato su tutte le interfacce, porta **8767** (configurabile come le altre:
  `app_port`, `CALAMAIO_APP_PORT`). Acceso solo se c'è almeno un telefono abbinato o una finestra di abbinamento
  aperta. 8765 resta solo `127.0.0.1` con le sue protezioni, invariato.
- Certificato **autofirmato** ECDSA P-256 generato al primo uso in `<dati>/app-tls/` (chiave 0600), validità 10 anni.
  L'app lo **fissa** (impronta SHA-256 dal QR): nessuna autorità di certificazione, nessuna eccezione TLS generica.
- Ogni richiesta: `Authorization: Bearer <token del telefono>`. Token di 256 bit; il servizio ne tiene solo lo
  SHA-256 in `<dati>/phones.json` (0600) con nome, data di abbinamento, ultimo uso. Token assente o sconosciuto:
  401 senza dettagli. Revoca dalla webapp.
- **Elenco chiuso** delle API raggiungibili dal telefono (il resto 404): `GET /`, file statici, `/api/status`,
  `/api/events`, sessioni (archivia/ripristina/avatar), avatar, gioco della mascotte, accessori, temi, impostazioni
  della scheda (`settings`, `full-refresh`, `workday`, `theme`), crash in sola lettura, Giochi (`/api/gb/*` quando
  esisterà). **Mai dal telefono**: `/hook*`, `/statusline`, `/debug*`, abbinamento/disabbinamento della scheda, Wi-Fi
  della scheda, OTA e immagini firmware, backup (contengono chiavi), registro remoto, abbinamento di altri telefoni.
- Una richiesta autenticata dal token vale come «stessa origine» per i controlli di `postGuard`/`deleteGuard` (un
  segno nel contesto della richiesta impostato solo dall'ascoltatore 8767); il controllo dell'Host non si applica su
  8767 (lì protegge il token).
- Annuncio Bonjour `_calamaio-app._tcp` su 8767, TXT `fp=<prime 16 cifre esadecimali dell'impronta>`.

### 3.2 Abbinamento con QR
1. Webapp › Dispositivo › **«Telefoni»** › «Abbina un telefono»: il servizio apre una finestra di 5 minuti e crea un
   codice monouso di 128 bit.
2. La webapp mostra un QR: `calamaio://pair?v=1&h=<ip1,ip2>&p=8767&fp=<impronta completa>&c=<codice>&n=<nome PC>`.
3. L'app lo inquadra (fotocamera), apre TLS fissato su `fp`, `POST /app/pair {code, name}` → `{token}`. Codice
   usato una volta sola, 5 tentativi sbagliati chiudono la finestra.
4. Il token va nel **Portachiavi** di iOS (solo questo dispositivo, dopo il primo sblocco). La webapp mostra il
   telefono nell'elenco con «Revoca».

### 3.3 La webapp dentro l'app
- **Scelta**: la `WKWebView` carica `calamaio-app://ui/` con un `WKURLSchemeHandler`: l'app inoltra **ogni**
  richiesta (pagine, JS, API, SSE in streaming) al servizio con TLS fissato e token. La webapp usa già indirizzi
  relativi: funziona senza modifiche e il token non entra mai nel JavaScript.
- Corpi delle richieste: se il gestore non riceve il corpo di `fetch`/`FormData` (limite noto di WebKit sugli schemi
  propri), un piccolo script iniettato manda i caricamenti tramite `messageHandlers`. Lo decide il primo compito del
  piano (prova).
- Caricamento file (ROM, salvataggi, avatar, temi): `<input type="file">` della webapp apre l'app File di iOS.
  Scaricamenti (export di temi e salvataggi): `WKDownload` → foglio di condivisione.
- Ricerca del PC: Bonjour con l'impronta abbinata; ripiego sugli indirizzi del QR; errore chiaro «PC non trovato:
  telefono e PC devono essere sulla stessa rete».

## 4. ROM e salvataggi dal telefono
Dipende dal **piano C dei giochi** (pagina Giochi, `/api/gb/*`, messaggi `gb-*`), non ancora eseguito. L'app non
aggiunge niente: la pagina Giochi caricata nell'app usa le stesse API (ROM ≤ 2 MiB, `.gb`/`.gbc`, controllo
dell'intestazione nel servizio). Requisito per il piano C: la pagina Giochi deve funzionare con il tocco e a 393 pt
di larghezza.

## 5. Webapp sul telefono
- Tutte le pagine usabili a 393×852 pt (iPhone 17 Pro): navigazione compatta, bersagli di tocco ≥ 44 pt, niente
  scorrimento orizzontale.
- Editor delle schermate: tela con pizzica/zoom, pannelli a cassetto; le funzioni restano tutte.
- Pagina Firmware nascosta nell'app (rilevata dal segno `navigator.userAgent` con «CalamaioApp/»).

## 6. Firmware
- `hw/blepad`: secondo tipo «telefono» (filtro di ricerca, report v1, mantenimento 3 s, due abbinamenti,
  sicurezza per tipo); `core/pad`: decodifica del report v1 testabile in nativo; sorgente nello stato.
- Schermata del codice di associazione (e-ink, parziale), toast nuovi: «Metti l'app Calamaio sulla scheda
  Controller», «Telefono collegato», «Telefono scollegato», «Codice per il telefono», «Associazione non riuscita»,
  «Per consentire usa il tasto PRG o il controller», «Telefono dimenticato». Testi nuovi rispetto al simulatore:
  allineamento annotato in DECISIONI.
- Regola §7 del consenso (2.3). Memoria: misurare heap e blocco massimo con il telefono collegato durante una
  partita; obiettivo nessun peggioramento rispetto all'Xbox.

## 7. Prove
- **Nativo (firmware)**: decodifica del report v1 (lunghezze/versioni sbagliate scartate), filtro di ricerca per
  tipo, due abbinamenti per tipo, mantenimento 3 s → tutto rilasciato, HoldFire+CONSENTI da telefono mai `allow`.
- **Go**: token (assente/sbagliato/revocato → 401), elenco chiuso (ogni rotta vietata → 404 da 8767), codice
  monouso e tentativi, certificato e permessi dei file, 8765 invariato.
- **Swift (XCTest)**: codifica del report, lettura del QR, verifica dell'impronta, gestore dello schema.
- **Dal vivo**: associazione con passkey; Xbox che si riassocia; latenza percepita in Pokémon; menu dal telefono;
  CONSENTI dal telefono rifiutato e NEGA accettato; abbinamento QR; webapp completa nell'app; revoca; 7 giorni
  (reinstallazione).

## 8. Ordine di lavoro
1. Prove dal vivo della fase 1b dell'editor (in sospeso).
2. **Fase P1** — servizio: ascoltatore 8767, certificato, abbinamento, elenco chiuso, sezione «Telefoni» della
   webapp. Indipendente dal piano C.
3. **Fase P2** — app: progetto, abbinamento QR, webapp nello schema proprio, prova dei corpi delle richieste.
4. **Fase P3** — controller: firmware (0.14.0) e scheda Controller dell'app; flash col permesso già dato.
5. **Fase P4** — webapp adatta al telefono.
6. Piano C dei giochi (pagina Giochi), poi ROM dal telefono senza lavoro in più nell'app.
