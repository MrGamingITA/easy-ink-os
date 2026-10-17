# M5 · Abbinamento e permessi dal display

Specifica di sicurezza. Firmware e servizio la seguono alla lettera; i messaggi entrano in `PROTOCOLLO.md` e nello schema. Ogni deviazione va in `DECISIONI.md`.

## Obiettivi

1. Solo il display abbinato può rispondere a un permesso. Un altro dispositivo in rete non deve poter approvare, neanche registrando e ripetendo il traffico.
2. Il display mostra solo richieste che vengono davvero dal servizio abbinato.
3. Nessuna approvazione automatica, mai. NEGA preselezionato, consentire richiede due gesti (brief §7).
4. Le domande di Claude (`AskUserQuestion`, stato `input`) non arrivano mai al display come permessi.
5. **Nessuna scadenza** (decisione dell'utente, vedi DECISIONI): la richiesta resta aperta finché qualcuno risponde, sul display o nel terminale. Vale la prima risposta.
6. I segreti sulla scheda sono cifrati con una chiave legata al chip (eFuse), così non sono leggibili da un backup della flash.

## Primitive

| Uso | Primitiva |
|---|---|
| Scambio di chiavi | X25519 |
| Derivazione | HKDF-SHA256 |
| Autenticazione dei messaggi | HMAC-SHA256, troncato a 16 byte (32 caratteri esadecimali) |
| Impegno | SHA-256 |
| Cifratura dei segreti sulla scheda | AES-256-GCM |

Tutte le stringhe binarie in JSON sono in esadecimale minuscolo. `‖` è la concatenazione; le etichette sono stringhe ASCII.

## 1. Abbinamento

Si avvia dal PC: `calamaio pair` (o il pulsante nella webapp). Se ci sono più display collegati, si sceglie quale. Serve un display collegato via WebSocket.

```
servizio → display   pair-begin   {id, commit = SHA256(pkS)}
display → servizio   pair-key     {id, pkD}
servizio → display   pair-reveal  {id, pkS}           display verifica SHA256(pkS) == commit
```

Entrambi calcolano:

- `Z = X25519(sk, pk dell'altro)`
- `T = SHA256("calamaio-pair-v1" ‖ board ‖ commit ‖ pkD ‖ pkS)`
- `K = HKDF(Z, salt = T, info = "calamaio-key")` → 32 byte, chiave di abbinamento
- `SAS = HKDF(Z, salt = T, info = "calamaio-sas")` → primi 4 byte come intero big-endian, modulo 1 000 000, a 6 cifre con zeri iniziali

Il display mostra la schermata «Abbinamento» con il codice a 6 cifre grande e «2X CONFERMA · LUNGA ANNULLA». `calamaio pair` chiede di scrivere il codice che si legge sul display.

- Codice sbagliato sul PC → `pair-abort {id, reason: "codice"}`, nessun segreto salvato, il display torna indietro con toast «Abbinamento annullato».
- Codice giusto → il servizio manda `pair-confirm {id, mac = HMAC(K, "pair-s" ‖ T)}`. Il display verifica `mac` e **aspetta la conferma fisica** (2 pressioni). Con la conferma risponde `pair-done {id, mac = HMAC(K, "pair-d" ‖ T)}` e salva K. Il servizio verifica e salva K.
- Il commit impedisce a chi sta in mezzo di scegliere la sua chiave dopo aver visto quella del display: indovinare il codice gli riesce una volta su un milione.
- Finestra di abbinamento: 3 minuti, poi `pair-abort {reason: "tempo"}`. Un solo abbinamento in corso per display.
- Un nuovo abbinamento sostituisce il vecchio (sul display e nel servizio). «Dimentica abbinamento» dal menu del display cancella K e lo dice al servizio con `unpair` (autenticato, vedi 2) se collegato.

## 2. Autenticazione della connessione

A ogni connessione WebSocket, dopo `hello`, se il servizio ha una K per quella board:

```
servizio → display   auth-challenge {ns}                  ns: 16 byte casuali
display → servizio   auth           {nd, mac = HMAC(K, "auth-d" ‖ board ‖ ns ‖ nd)}
servizio → display   auth-ok        {mac = HMAC(K, "auth-s" ‖ board ‖ nd ‖ ns)}
```

Chiave di sessione: `SK = HKDF(K, salt = ns ‖ nd, info = "calamaio-session")`.

- Il display considera **abbinato e autenticato** il servizio solo dopo un `auth-ok` valido. Senza, mostra i dati ma non accetta richieste di permesso né comandi `set` (le impostazioni restano modificabili solo dal menu).
- Il servizio considera il display autenticato solo dopo un `auth` valido; prima non gli manda `perm-request`.
- Se il display ha K ma il servizio non ce l'ha (o viceversa), il display mostra in Stato e nel menu «Abbinamento: da rifare».

## 3. Permessi

`perm-request` (servizio → display), solo se: display autenticato, `permOn` vero nelle sue impostazioni, evento `PermissionRequest` con strumento diverso da `AskUserQuestion`.

```
{t, v, id, session, name, tool, arg, at, mac}
mac = HMAC(SK, "req" ‖ id ‖ session ‖ name ‖ tool ‖ arg ‖ at)      (campi testuali separati da 0x1F)
```

- `id`: 16 byte casuali in esadecimale, unico.
- `arg`: argomento corto, al massimo 96 byte; `tool` al massimo 63 byte; `name` al massimo 31 byte: tutti troncati in modo sicuro per UTF-8 prima di calcolare il mac. Il nome mostrato è firmato: un intruso non può far apparire la richiesta come se venisse da un altro progetto.

`perm-reply` (display → servizio):

```
{t, v, id, decision: "allow"|"deny", nonce, mac}
nonce: 16 byte casuali
mac = HMAC(SK, "rep" ‖ id ‖ decision ‖ nonce)
```

Il servizio accetta la risposta solo se: viene dalla connessione autenticata di un display abbinato, il mac è valido, l'id esiste ed è ancora in attesa, il nonce non è già stato visto. Altrimenti la scarta e scrive nel log perché. Risposta accettata → il hook in attesa stampa la decisione (`deny` con `message: "Negato dal display Calamaio"`) e a tutti i display va `perm-close`.

`perm-close` (servizio → display): `{id, reason, mac}` con `mac = HMAC(SK, "close" ‖ id ‖ reason)` (campi separati da 0x1F); il display lo accetta solo su connessione autenticata e con mac valido. `reason` è uno tra `display` (risposto da un display), `terminal` (risposto nel terminale: il hook è stato chiuso da Claude Code), `session` (sessione chiusa), `service` (servizio in arresto). Il display chiude la richiesta e, se era aperta sullo schermo, mostra il toast «Risposto altrove».

**Nessuna scadenza**: `calamaio hook --wait` aspetta senza limite (il hook ha `timeout` 86400 s nel plugin). Se Claude Code chiude il hook (risposta nel terminale, interruzione), la richiesta HTTP si chiude e il servizio manda `perm-close {reason: terminal}`. Il conto alla rovescia «20 S» della schermata Permesso diventa il tempo trascorso dall'arrivo della richiesta («da 1 MIN»): il testo va allineato nel simulatore.

Più richieste insieme: il display le mette in coda nell'ordine di arrivo; la Home e il banner mostrano la sessione più urgente come oggi.

## 4. Segreti sulla scheda

- **Chiave madre in eFuse**: uno spazio chiave libero (il primo con scopo USER e contenuto a zero) programmato con 32 byte casuali dal generatore hardware, scopo `HMAC_UP`, protetto dalla lettura. Operazione irreversibile: si fa solo da Menu › Sicurezza › «Proteggi le chiavi», con avviso, 2 pressioni e poi «tieni 3 s». Mai in automatico.
- **Chiave di cifratura**: `KE = HMAC_eFuse("calamaio-nvs" ‖ sale)`, con `sale` di 16 byte casuali salvato in NVS. Ruotare = sale nuovo e ri-cifrare; «Ripristino di fabbrica» = cancellare i segreti e il sale.
- **Cosa si cifra** (AES-256-GCM, nonce casuale di 12 byte, versione nel record): password Wi-Fi, K di abbinamento. Lo SSID può restare in chiaro.
- **Prima di proteggere le chiavi** (eFuse non programmata): i segreti restano in NVS come oggi e Menu › Sicurezza lo dice («Chiavi non protette»). Dopo la protezione, i segreti esistenti vengono ri-cifrati subito.
- **Copia in chiaro del Wi-Fi**: il core Arduino salva di suo SSID e password nella NVS di sistema. Va disattivato (`WiFi.persistent(false)`) e la copia esistente va cancellata.
- **Backup su un'altra scheda**: i segreti non si decifrano; la scheda li tratta come assenti (configurazione Wi-Fi e abbinamento da rifare).

## 5. Lato servizio

- K per board in `config.json` (permessi 0600) sotto `paired`: `{board, key, created}`. In futuro il portachiavi del sistema.
- `calamaio pair [--board B]`, `calamaio unpair B`, `calamaio status` mostra abbinamento e autenticazione per display.
- Webapp: stato dell'abbinamento in Dispositivo, pulsante «Abbina» che avvia il flusso e chiede il codice nella pagina.

## Test di accettazione (brief §7, aggiornati)

1. Consenti dal display.
2. Nega dal display.
3. Richiesta lasciata aperta a lungo (minuti): resta aperta, nessuna scadenza.
4. Annulla con la pressione lunga: chiude la schermata Permesso senza rispondere; la richiesta resta valida nel terminale e sul display, raggiungibile di nuovo dal Dettaglio della sessione (2×).
5. Rispondi dal terminale mentre il display aspetta: il display chiude la richiesta («Risposto altrove»).
6. Display spento o fuori rete: la domanda resta nel terminale; al ritorno, se ancora aperta, il display la riceve.
7. Risposta falsa da un altro client in rete (mac sbagliato, id inesistente, nonce ripetuto, connessione non autenticata): rifiutata e registrata.
8. Abbinamento con codice sbagliato: rifiutato, nessun segreto salvato.
9. Backup della flash dopo «Proteggi le chiavi»: nessuna password o K in chiaro (ricerca delle stringhe note nel dump).
