// Schermate da catturare sulla Heltec Wireless Paper (classe S, 250x122, 1 tasto PRG).
//
// Ogni passo e una stringa:
//   '1x' '2x' '3x'   pressioni brevi del tasto PRG (keyDown/keyUp del componente, poi la
//                    finestra di 380 ms del simulatore che le conta e chiama gesture())
//   'long'           pressione lunga (0,8 s): gesture('long') -> act('back', true)
//   'vlong'          tasto tenuto 3,1 s: gesture('vlong') -> act('sleep')
//   'down' / 'up'    tasto giù (e resta giù) / su: per catturare con il tasto ancora premuto
//   'wait:<ms>'      fa avanzare l'orologio congelato di <ms> millisecondi
//   'sim:<id>'       pulsante del pannello Simula (simClicks.<id>): pc, limit, air, pcoff, perm...
//                    M3: empty (nessuna sessione), nolimits (limiti assenti), joinfail
//                    (connessione fallita), elsewhere (permesso risposto nel terminale), prompt
//                    (nuovo prompt: riporta una sessione archiviata)
//                    M5: pair (abbinamento dal PC: codice), pairok (il PC ha il codice giusto),
//                    pairbad (codice sbagliato), redo (abbinamento da rifare)
//                    Batteria in alto: battlvl (livello 100 -> 50 -> 10 %, a giro; da 78 va a 100)
//                    Avatar: avatar (avatar della sessione mostrata: predefinito -> gatto -> demone ->
//                    calamaro -> predefinito), defavatar (avatar predefinito: goccia -> gatto -> demone ->
//                    calamaro -> goccia). Sessioni finte: api-server Gatto, web-app Demone,
//                    firmware-esp32 Calamaro, docs-site senza avatar (= predefinito).
//                    M6: battcrit (batteria sotto la soglia critica senza cavo: schermata «Batteria scarica»),
//                    calsaved (calibrazione già salvata, 8 %/h e 1,5 %/h, per non aspettare 2 ore)
//                    OTA: ota (aggiornamento dal PC a giro: 0% → 37% → 80% → verifica → riavvio → errore →
//                    rifiutato → chiuso)
//                    0.8.0 (più reti Wi-Fi): scenari reti-salvate*, rete-salvata-* con le 4 reti finte di
//                    m.saved (CasaWiFi in uso, Officina visibile, Casa Mare, Laboratorio mai usata)
//                    0.10.0 (gioco della mascotte): game (impostazione remota `game`, a giro), petlevel, petgoal,
//                    petwear, pettired: fanno le veci del messaggio `game` del servizio
//                    0.10.1: noclock (ora non ancora nota: «--:--» e niente data, a giro)
//                    Gli scenari calibra-fase2 e calibra-fatta aspettano 1 e 2 ore di orologio finto: la
//                    cattura completa dura circa 25 minuti.
// Nessuna schermata qui sotto manipola il modello direttamente: ci si arriva solo con
// gesti e pulsanti Simula, come farebbe l'utente.

// Il boot dura 2600 ms (newModel: ui.stack = [{ id: 'boot', until: t + 2600 }]); tick()
// passa alla Home al primo giro di 100 ms dopo la scadenza, quindi a 2700 ms si e in Home.
const BOOT = 'wait:2700';
const TO_DISPLAY = ['3x', '1x', '1x', '1x', '1x', '2x']; // Menu, giu fino a Display (voce 4; 0.11.0: Gioco è la voce 2), apri
const TO_STATO = ['3x', '1x', '1x', '1x', '1x', '1x', '1x', '1x', '1x', '1x', '1x', '2x']; // voce 10 = Stato (solo S; M5: Sicurezza è la voce 6)
// Aggiunte M2: n volte lo stesso passo; voce k del Menu principale (3x, k volte 1x, 2x).
const times = (n, st) => Array(n).fill(st);
const menuItem = (k) => ['3x', ...times(k, '1x'), '2x'];

export const SCENARIOS = [
  { name: 'boot-meta', steps: ['wait:1300'], note: 'Boot a meta (el = 0,5): Schermo ok, Wi-Fi ok, PC in attesa; mascotte che lavora.' },
  { name: 'boot-fine', steps: ['wait:2500'], note: 'Boot quasi finito (el = 0,96 > 0,85): tutto ok, mascotte contenta.' },

  { name: 'home-goccia', steps: [BOOT], note: 'Home stile Goccia (predefinito). Sessione mostrata = la piu urgente (web-app, PERMESSO).' },
  { name: 'home-righe', steps: [BOOT, ...TO_DISPLAY, '2x', '3x'], note: 'Menu > Display > Stile home premuto 1 volta (Goccia -> Righe), poi 3x torna alla Home.' },
  { name: 'home-contatori', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x'], note: 'Stile home premuto 2 volte (-> Contatori).' },
  { name: 'home-una', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '2x', '3x'], note: 'Stile home premuto 3 volte (-> Una alla volta). Con una sessione in attesa mostra la piu urgente.' },

  { name: 'dettaglio-web-app', steps: [BOOT, '2x'], note: 'Dettaglio della sessione in PERMESSO (2x dalla Home apre la sessione mostrata).' },
  { name: 'dettaglio-firmware-esp32', steps: [BOOT, '1x', '2x'], note: 'Dettaglio di una sessione che LAVORA (1x seleziona la successiva).' },
  { name: 'dettaglio-docs-site', steps: [BOOT, '1x', '1x', '2x'], note: 'Dettaglio di una sessione FINITA.' },

  { name: 'permesso-nega', steps: [BOOT, '2x', '2x'], note: 'Permesso appena aperto: NEGA preselezionato, «IN ATTESA DA 1 MIN» al posto del conto alla rovescia (M5).' },
  { name: 'permesso-consenti', steps: [BOOT, '2x', '2x', '1x'], note: 'Permesso dopo 1x: scelta su CONSENTI.' },
  { name: 'permesso-consentito', steps: [BOOT, '2x', '2x', '1x', '2x'], note: 'Dopo 2x su CONSENTI: torna al Dettaglio con il toast «Consentito · inviato a Claude Code».' },

  { name: 'menu', steps: [BOOT, '3x'], note: 'Menu principale, prima voce selezionata.' },
  { name: 'menu-display', steps: [BOOT, ...TO_DISPLAY], note: 'Sottomenu Display (con Stile home, solo classe S).' },
  { name: 'stato', steps: [BOOT, ...TO_STATO], note: 'Schermata Stato (solo classe S): Wi-Fi, batteria, PC.' },
  { name: 'riposo', steps: [BOOT, 'vlong'], note: 'Riposo (sleepScreen): tasto tenuto 3 s dalla Home.' },

  { name: 'offline-toast', steps: [BOOT, 'sim:pc'], note: 'Simula «PC scollegato»: schermata PC NON RAGGIUNGIBILE con il toast.' },
  { name: 'offline', steps: [BOOT, 'sim:pc', 'wait:2700'], note: 'PC non raggiungibile dopo la scadenza del toast (2600 ms).' },
  { name: 'pc-spento', steps: [BOOT, 'sim:pcoff'], note: 'Simula «PC spento»: riepilogo fermo (riepilogo, ramo S).' },

  { name: 'toast-silenziato', steps: [BOOT, 'long'], note: 'Pressione lunga nella Home: silenzia 1 ora, toast «Silenziato per 1 ora» e campanella barrata.' },

  { name: 'home-goccia-stanca', steps: [BOOT, 'sim:limit', 'wait:2700', '1x'], note: 'Simula «Limite 5h al 92%», toast scaduto, 1x seleziona firmware-esp32 (LAVORA): posa STANCA (pose() = tired se h5 >= 90 e stato working/done/idle) e icona contatore.' },
  { name: 'home-goccia-stanca-toast', steps: [BOOT, 'sim:limit'], note: 'Subito dopo «Limite 5h al 92%»: toast «Limite 5 ore al 92%». La sessione mostrata e in PERMESSO, quindi niente posa stanca.' },
  { name: 'home-una-stanca', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '2x', '3x', 'sim:limit', 'wait:2700', '1x'], note: 'Una alla volta con limite 5h al 92% e sessione che lavora selezionata: mascotte grande STANCA.' },
  { name: 'home-goccia-aria', steps: [BOOT, 'sim:air', 'wait:2700', '1x'], note: 'Simula «Aria viziata» (CO2 1250): posa BOCCHEGGIA (stuffy) sulla sessione che lavora.' },
  { name: 'home-goccia-lavora', steps: [BOOT, '1x'], note: 'Goccia con firmware-esp32 (LAVORA) selezionata: posa lavora.' },
  { name: 'home-goccia-finito', steps: [BOOT, '1x', '1x'], note: 'Goccia con docs-site (FINITO) selezionata: posa contenta.' },

  // ---- M2: viste raggiungibili dal menu in classe S (aggiunte per il confronto del firmware) ----
  { name: 'limiti', steps: [BOOT, ...menuItem(0)], note: 'Menu > Limiti: 5 ore e 7 giorni, contesto piu alto.' },
  { name: 'menu-strumenti', steps: [BOOT, ...menuItem(1)], note: 'Menu > Strumenti (sottomenu).' },
  { name: 'menu-gioco', steps: [BOOT, ...menuItem(2)], note: 'Menu › Gioco: solo «Alto o basso» (il simulatore non ha il controller Bluetooth).' },
  { name: 'menu-wifi', steps: [BOOT, ...menuItem(3)], note: 'Menu > Wi-Fi (sottomenu con barre del segnale).' },
  { name: 'menu-avvisi', steps: [BOOT, ...menuItem(5)], note: 'Menu > Avvisi (sottomenu).' },
  { name: 'oggi', steps: [BOOT, ...menuItem(7)], note: 'Menu > Oggi: sei riquadri.' },
  { name: 'storico', steps: [BOOT, ...menuItem(8)], note: 'Menu > Storico: ultimi eventi con barra di scorrimento.' },
  { name: 'storico-scorre', steps: [BOOT, ...menuItem(8), '1x'], note: 'Storico dopo 1x: scorre di 3 righe.' },
  { name: 'batteria', steps: [BOOT, ...menuItem(9), '2x'], note: 'Menu > Batteria > Livello (M6: Batteria è un sottomenu). Senza calibrazione e a batteria: «Autonomia: stima in corso».' },
  { name: 'batteria-risparmio', steps: [BOOT, ...menuItem(9), '2x', '2x'], note: 'Batteria dopo 2x: risparmio energetico attivo.' },
  { name: 'menu-info', steps: [BOOT, ...menuItem(12)], note: 'Menu > Info (sottomenu, scorre).' },
  { name: 'menu-fondo', steps: [BOOT, '3x', ...times(12, '1x')], note: 'Menu principale con l ultima voce (Info) selezionata: lista scorsa in fondo.' },
  { name: 'concentrazione', steps: [BOOT, ...menuItem(1), '2x'], note: 'Strumenti > Concentrazione, timer fermo.' },
  { name: 'concentrazione-attiva-toast', steps: [BOOT, ...menuItem(1), '2x', '2x'], note: 'Concentrazione avviata (2x): toast con l ora di fine.' },
  { name: 'concentrazione-attiva', steps: [BOOT, ...menuItem(1), '2x', '2x', 'wait:2700'], note: 'Concentrazione avviata, toast scaduto.' },
  { name: 'agenda', steps: [BOOT, ...menuItem(1), '1x', '2x'], note: 'Strumenti > Agenda.' },
  { name: 'servizi', steps: [BOOT, ...menuItem(1), '1x', '1x', '2x'], note: 'Strumenti > Servizi e PR.' },
  { name: 'servizi-scorre', steps: [BOOT, ...menuItem(1), '1x', '1x', '2x', '1x'], note: 'Servizi e PR dopo 1x: scorre di 2 righe.' },
  { name: 'ospiti', steps: [BOOT, ...menuItem(1), '1x', '1x', '1x', '2x'], note: 'Strumenti > Wi-Fi ospiti (QR finto).' },
  { name: 'ambiente-nessuno', steps: [BOOT, ...menuItem(1), ...times(4, '1x'), '2x'], note: 'Strumenti > Ambiente senza sensore.' },
  { name: 'ambiente-co2', steps: [BOOT, 'sim:air', 'wait:2700', ...menuItem(1), ...times(4, '1x'), '2x'], note: 'Ambiente con CO2 a 1250 ppm: Goccia boccheggia.' },
  { name: 'ricerca-reti', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x'], note: 'Wi-Fi > Cerca reti: ricerca in corso (1,5 s).' },
  { name: 'reti-vicine', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600'], note: 'Reti vicine dopo la ricerca: CasaWiFi con la spunta.' },
  { name: 'reti-qr-casawifi', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600', '2x'], note: 'Rete protetta scelta: QR con «SCEGLI CASAWIFI».' },
  { name: 'connessione-ospiti', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600', '1x', '1x', '2x'], note: 'Rete aperta Ospiti: Connessione a Ospiti.' },
  { name: 'connesso-ospiti', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600', '1x', '1x', '2x', 'wait:2700'], note: 'Connessione finita: torna alla lista con il toast «Connesso a Ospiti».' },
  { name: 'qr-telefono', steps: [BOOT, ...menuItem(3), ...times(5, '1x'), '2x'], note: 'Wi-Fi > Configura da telefono: QR senza rete scelta.' },
  { name: 'permesso-attesa', steps: [BOOT, '2x', '2x', 'wait:65000'], note: 'M5, nessuna scadenza: dopo un altro minuto «IN ATTESA DA 2 MIN» (web-app aspettava da 1 minuto).' },
  { name: 'home-invertita', steps: [BOOT, ...TO_DISPLAY, ...times(4, '1x'), '2x', '3x'], note: 'Display > Inverti colori: si, poi Home (compose inverte).' },
  { name: 'home-ruotata', steps: [BOOT, ...TO_DISPLAY, ...times(5, '1x'), '2x', '3x'], note: 'Display > Ruota 180: si, poi Home (compose ribalta).' },
  { name: 'home-righe-scelta', steps: [BOOT, ...TO_DISPLAY, '2x', '3x', '1x'], note: 'Righe con una sessione scelta (1x): tacche ai lati della barra.' },
  { name: 'home-batteria-bassa', steps: [BOOT, 'sim:batt', 'sim:batt', 'sim:batt', 'sim:batt', 'wait:2700'], note: 'Batteria al 3%: icona della batteria tra gli avvisi, risparmio automatico.' },
  { name: 'stato-senza-wifi', steps: [BOOT, 'sim:wifi', 'wait:2700', ...TO_STATO], note: 'Wi-Fi assente (e quindi PC): schermata Stato.' },
  { name: 'offline-senza-wifi', steps: [BOOT, 'sim:wifi', 'wait:2700'], note: 'Wi-Fi assente: PC NON RAGGIUNGIBILE con spina barrata e Wi-Fi barrato tra gli avvisi.' },
  { name: 'batteria-usb', steps: [BOOT, 'sim:usb', 'wait:2700', ...menuItem(9), '2x'], note: 'USB-C collegata: Batteria con il fulmine, «In carica dalla USB-C» e «Autonomia: calibra per una stima».' },

  // ---- M3: stati che sul firmware arrivano dalla rete vera, portati nel simulatore (brief 2.2) ----
  { name: 'home-vuota', steps: [BOOT, 'sim:empty'], note: 'Simula «Nessuna sessione» (state con sessions vuoto): NESSUNA / sessione aperta / Apri Claude Code sul PC…' },
  { name: 'home-vuota-tasto', steps: [BOOT, 'sim:empty', '2x'], note: 'Home senza sessioni dopo 2x: niente da aprire («Nessuna sessione»).' },
  { name: 'menu-limiti-assenti', steps: [BOOT, 'sim:nolimits', '3x'], note: 'Simula «Limiti assenti» (limits null dal PC): voce Limiti del Menu con «5h —».' },
  { name: 'limiti-assenti', steps: [BOOT, 'sim:nolimits', ...menuItem(0)], note: 'Limiti senza dati dal PC: «—» e «nessun dato dal PC» per 5 ore e 7 giorni.' },
  { name: 'limiti-assenti-vuota', steps: [BOOT, 'sim:empty', 'sim:nolimits', ...menuItem(0)], note: 'Limiti senza dati e senza sessioni: manca anche la riga del contesto.' },
  { name: 'connessione-fallita', steps: [BOOT, ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600', '1x', '1x', '2x', 'sim:joinfail'], note: 'Connessione a Ospiti fallita (Simula «Connessione fallita»): torna alle reti con il toast «Connessione non riuscita».' },
  { name: 'risposto-altrove', steps: [BOOT, '2x', '2x', 'sim:elsewhere'], note: 'Permesso aperto e risposto nel terminale (Simula «Risposto nel terminale»): torna al Dettaglio con «Risposto altrove».' },
  { name: 'dimentica-conferma', steps: [BOOT, ...menuItem(3), ...times(8, '1x'), '2x'], note: 'Wi-Fi > Dimentica tutte le reti premuta una volta: valore «conferma?» e toast «2 pressioni ancora per confermare».' },
  { name: 'dimentica-confermata', steps: [BOOT, ...menuItem(3), ...times(8, '1x'), '2x', '2x'], note: 'Seconda doppia pressione entro 6 s: «Reti dimenticate».' },
  // 0.8.0: più reti Wi-Fi. Menu › Wi-Fi › Reti salvate (voce 3): CasaWiFi in uso, Officina visibile, Casa Mare e
  // Laboratorio non visibili (Laboratorio mai usata). Una rete: Segnale, Ultimo uso, Dimentica (doppia conferma).
  { name: 'reti-salvate', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x'], note: 'Wi-Fi > Reti salvate: 4 reti, «in uso» e segnale per quelle che si vedono.' },
  { name: 'reti-salvate-sel-3', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', ...times(3, '1x')], note: 'Reti salvate: Laboratorio selezionata (non visibile, senza barre).' },
  { name: 'rete-salvata-casawifi', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '2x'], note: 'CasaWiFi: segnale vero, ultimo uso «in uso».' },
  { name: 'rete-salvata-officina', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '1x', '2x'], note: 'Officina: −66 dBm, ultimo uso 08/10.' },
  { name: 'rete-salvata-laboratorio', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', ...times(3, '1x'), '2x'], note: 'Laboratorio: non visibile, ultimo uso «mai».' },
  { name: 'rete-salvata-dimentica-conferma', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '1x', '2x', '1x', '1x', '2x'], note: 'Officina > Dimentica premuta una volta: «conferma?» e toast.' },
  { name: 'rete-salvata-dimenticata', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '1x', '2x', '1x', '1x', '2x', '2x'], note: 'Seconda doppia pressione: Officina tolta, di nuovo in Reti salvate con «Rete dimenticata».' },
  { name: 'rete-salvata-in-uso-dimenticata', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '2x', '1x', '1x', '2x', '2x', 'wait:2700'], note: 'Dimenticata la rete in uso (CasaWiFi): niente Wi-Fi né PC, sul firmware riavvio dopo 3 s.' },
  { name: 'rete-salvata-dimentica-scaduta', steps: [BOOT, ...menuItem(3), ...times(3, '1x'), '2x', '1x', '2x', '1x', '1x', '2x', 'wait:6200'], note: 'Senza conferma entro 6 s la voce torna normale.' },
  { name: 'reti-salvate-vuote', steps: [BOOT, ...menuItem(3), ...times(8, '1x'), '2x', '2x', ...times(4, '1x'), '2x'], note: 'Dopo Dimentica tutte le reti: Reti salvate «0 di 5» con la voce «Nessuna rete».' },
  // ---- Archivia (richiesta dell'utente): 2x nel Dettaglio di una sessione ferma (done, idle, error) ----
  { name: 'dettaglio-finita-archivia', steps: [BOOT, '1x', '1x', '2x'], note: 'Dettaglio di docs-site (FINITA): riga in fondo «1X SESSIONE   2X ARCHIVIA   LUNGA INDIETRO».' },
  { name: 'archivia', steps: [BOOT, '1x', '1x', '2x', '2x'], note: 'Archivia docs-site: toast «Archiviata», Home con 3 sessioni.' },
  { name: 'archivia-contatori', steps: [BOOT, '1x', '1x', '2x', '2x', 'wait:2700', ...TO_DISPLAY, '2x', '2x', '3x'], note: 'Contatori dopo aver archiviato docs-site: FINITE 0.' },
  { name: 'archivia-ritorna', steps: [BOOT, '1x', '1x', '2x', '2x', 'wait:2700', 'sim:prompt'], note: 'Simula «Nuovo prompt»: docs-site torna al lavoro in fondo all elenco.' },
  // ---- M5: Risparmio energetico, Sicurezza, abbinamento, permessi senza scadenza ----
  { name: 'menu-display-risparmio', steps: [BOOT, ...TO_DISPLAY, ...times(3, '1x'), '2x'], note: 'Display > Risparmio energetico: sì (dopo Velocità animazioni).' },
  { name: 'home-risparmio', steps: [BOOT, ...TO_DISPLAY, ...times(3, '1x'), '2x', '3x'], note: 'Home con il risparmio energetico: Goccia ferma sul fotogramma 0 (f1 = f0, frameFor).' },
  { name: 'permesso-annullato', steps: [BOOT, '2x', '2x', 'long'], note: 'Pressione lunga nel permesso: «Annullato · resta in attesa», la richiesta resta aperta (Dettaglio, 2×).' },
  { name: 'menu-sicurezza', steps: [BOOT, ...menuItem(6)], note: 'Menu > Sicurezza: abbinato a calamaio.local, chiavi non protette.' },
  { name: 'menu-sicurezza-da-rifare', steps: [BOOT, 'sim:redo', ...menuItem(6)], note: 'Simula «Abbinamento da rifare»: valore «da rifare».' },
  { name: 'stato-da-rifare', steps: [BOOT, 'sim:redo', ...TO_STATO], note: 'Stato con l\'abbinamento da rifare (M5 §2).' },
  { name: 'dimentica-abbinamento-conferma', steps: [BOOT, ...menuItem(6), '1x', '2x'], note: 'Dimentica abbinamento premuta una volta: «conferma?» e toast.' },
  { name: 'dimentica-abbinamento', steps: [BOOT, ...menuItem(6), '1x', '2x', '2x'], note: 'Seconda doppia pressione: «Abbinamento dimenticato», valore «non abbinato».' },
  { name: 'permesso-senza-abbinamento', steps: [BOOT, ...menuItem(6), '1x', '2x', '2x', '3x', '2x', '2x'], note: 'Senza abbinamento il Dettaglio dice RISPONDI NEL TERMINALE e 2× dà «Rispondi nel terminale».' },
  { name: 'abbinamento-codice', steps: [BOOT, 'sim:pair'], note: 'Simula «Abbinamento dal PC»: codice a 6 cifre da scrivere sul PC.' },
  { name: 'abbinamento-prima-del-codice', steps: [BOOT, 'sim:pair', '2x'], note: '2× prima che il PC abbia il codice: «Prima scrivi il codice sul PC».' },
  { name: 'abbinamento-conferma', steps: [BOOT, 'sim:pair', 'sim:pairok'], note: 'Il PC ha il codice giusto (pair-confirm): codice incorniciato, si conferma con 2×.' },
  { name: 'abbinamento-fatto', steps: [BOOT, 'sim:pair', 'sim:pairok', '2x'], note: '2×: «Abbinato».' },
  { name: 'abbinamento-annullato', steps: [BOOT, 'sim:pair', 'long'], note: 'Pressione lunga: «Abbinamento annullato».' },
  { name: 'abbinamento-codice-sbagliato', steps: [BOOT, 'sim:pair', 'sim:pairbad'], note: 'Codice sbagliato sul PC (pair-abort): «Abbinamento annullato».' },
  { name: 'proteggi-avviso', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x'], note: 'Proteggi le chiavi: avviso, «2X PER CONTINUARE».' },
  { name: 'proteggi-senza-2x', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', 'vlong'], note: 'Tenuto 3 s senza 2× prima: niente, «Prima 2X, poi tieni 3 s» (e niente Riposo).' },
  { name: 'proteggi-armato', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x'], note: 'Dopo 2×: «TIENI PREMUTO 3 SECONDI».' },
  { name: 'proteggi-fatto', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x', 'vlong'], note: 'Tenuto 3 s: «Chiavi protette», valore «protette».' },
  { name: 'ripristino-avviso', steps: [BOOT, ...menuItem(6), '1x', '1x', '1x', '2x'], note: 'Ripristino di fabbrica: avviso.' },
  { name: 'ripristino-armato', steps: [BOOT, ...menuItem(6), '1x', '1x', '1x', '2x', '2x'], note: 'Ripristino di fabbrica dopo 2×.' },
  { name: 'ripristino-fatto', steps: [BOOT, ...menuItem(6), '1x', '1x', '1x', '2x', '2x', 'vlong', 'wait:2700'], note: 'Tenuto 3 s: si riparte da zero (avvio, poi PC non raggiungibile senza Wi-Fi).' },
  // ---- Batteria in alto (classe S): icona e percentuale accanto all'orologio, fulmine in carica ----
  { name: 'home-batt-piena', steps: [BOOT, 'sim:battlvl'], note: 'Batteria al 100%: «100%» a sinistra dell orologio nella Home Goccia.' },
  { name: 'home-batt-50', steps: [BOOT, 'sim:battlvl', 'sim:battlvl'], note: 'Batteria al 50%.' },
  { name: 'home-batt-10', steps: [BOOT, ...times(3, 'sim:battlvl'), 'wait:2700'], note: 'Batteria al 10% senza USB: «10%» in alto, l icona non si ripete tra gli avvisi; risparmio automatico (Goccia ferma).' },
  { name: 'home-batt-10-toast', steps: [BOOT, ...times(3, 'sim:battlvl')], note: 'Subito dopo: toast «Batteria bassa · risparmio attivo».' },
  { name: 'home-in-carica', steps: [BOOT, 'sim:usb', 'wait:2700'], note: 'USB-C collegata: fulmine a sinistra dell icona, 78%.' },
  { name: 'home-in-carica-piena', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700'], note: 'Caso piu largo: fulmine e «100%».' },
  { name: 'home-contatori-batt-10', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x', ...times(3, 'sim:battlvl'), 'long', 'wait:2700'], note: 'Contatori al 10% con campanella barrata: nome accorciato prima di avvisi, batteria e orologio.' },
  { name: 'home-contatori-in-carica', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', ...TO_DISPLAY, '2x', '2x', '3x'], note: 'Contatori con fulmine e 100%.' },
  { name: 'home-righe-batt-10', steps: [BOOT, ...TO_DISPLAY, '2x', '3x', ...times(3, 'sim:battlvl'), 'wait:2700'], note: 'Righe al 10%: nessuna batteria in alto (righe intere), resta l icona tra gli avvisi in basso a destra.' },
  { name: 'home-una-batt-10', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '2x', '3x', ...times(3, 'sim:battlvl'), 'wait:2700'], note: 'Una alla volta al 10%: come Righe, solo l icona tra gli avvisi.' },
  { name: 'menu-in-carica-piena', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', '3x'], note: 'Striscia del Menu con fulmine e 100%.' },
  { name: 'menu-display-in-carica-piena', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', ...TO_DISPLAY], note: 'Display: titolo e valori con la striscia piu affollata.' },
  { name: 'ripristino-in-carica-piena', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', ...menuItem(6), '1x', '1x', '1x', '2x'], note: 'Titolo lungo «RIPRISTINO DI FABBRICA» con fulmine e 100%.' },
  { name: 'reti-vicine-batt-10', steps: [BOOT, ...times(3, 'sim:battlvl'), 'wait:2700', ...menuItem(3), ...times(4, '1x'), '2x', 'wait:1600'], note: 'Reti vicine al 10%.' },
  { name: 'dettaglio-in-carica-piena', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', '1x', '2x'], note: 'Dettaglio: batteria a sinistra di «2/4», nome accorciato prima del fulmine.' },
  { name: 'dettaglio-batt-10', steps: [BOOT, ...times(3, 'sim:battlvl'), 'wait:2700', '1x', '1x', '2x'], note: 'Dettaglio di docs-site al 10%.' },
  { name: 'offline-batt-10', steps: [BOOT, ...times(3, 'sim:battlvl'), 'sim:pc', 'wait:2700'], note: 'PC non raggiungibile al 10%: niente orologio, la batteria resta tra gli avvisi.' },
  { name: 'stato-in-carica', steps: [BOOT, 'sim:usb', 'wait:2700', ...TO_STATO], note: 'Stato in carica: striscia e riga «78% · in carica».' },
  // ---- Tenuto 3 s con l'indicatore (richiesta dell'utente): barra mentre il tasto è giù, azione a 3 s ----
  { name: 'tieni-riposo-0-5', steps: [BOOT, 'down', 'wait:500'], note: 'Tasto giù da 0,5 s: ancora niente (l indicatore compare a 0,7 s).' },
  { name: 'tieni-riposo-meta', steps: [BOOT, 'down', 'wait:1500'], note: 'Tasto giù da 1,5 s nella Home: «TIENI PREMUTO · RIPOSO», barra a metà.' },
  { name: 'tieni-riposo-fatto', steps: [BOOT, 'down', 'wait:3200'], note: 'A 3 s il Riposo parte con il tasto ancora giù: «FATTO · RILASCIA».' },
  { name: 'tieni-riposo-rilasciato', steps: [BOOT, 'down', 'wait:3200', 'up', 'wait:120'], note: 'Rilascio dopo il 3 s: nessun gesto, resta il Riposo.' },
  { name: 'tieni-menu-lunga', steps: [BOOT, '3x', 'down', 'wait:1000'], note: 'Nel Menu, tasto giù da 1 s: indicatore del Riposo (rilasciando ora torna indietro).' },
  { name: 'tieni-menu-lunga-rilasciata', steps: [BOOT, '3x', 'down', 'wait:1000', 'up', 'wait:120'], note: 'Rilascio a 1 s: pressione lunga, Indietro (Home).' },
  { name: 'proteggi-tieni-meta', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x', 'down', 'wait:2000'], note: 'Proteggi armato, tasto giù da 2 s: «TIENI PREMUTO · PROTEGGI LE CHIAVI».' },
  { name: 'proteggi-tieni-fatto', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x', 'down', 'wait:3300'], note: 'A 3 s: chiavi protette subito, «FATTO · RILASCIA» sopra Sicurezza.' },
  { name: 'proteggi-troppo-presto', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x', 'long'], note: 'Proteggi armato, rilasciato a 0,8 s: toast «Troppo presto: tieni fino in fondo», resta armato.' },
  { name: 'proteggi-troppo-presto-poi-fatto', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', '2x', 'long', 'wait:2700', 'vlong'], note: 'Dopo il «troppo presto» la conferma è ancora armata: il tenuto 3 s protegge le chiavi.' },
  { name: 'ripristino-tieni-meta', steps: [BOOT, ...menuItem(6), '1x', '1x', '1x', '2x', '2x', 'down', 'wait:1500'], note: 'Ripristino armato, tasto giù da 1,5 s.' },
  { name: 'proteggi-non-armato-tieni', steps: [BOOT, ...menuItem(6), '1x', '1x', '2x', 'down', 'wait:1500'], note: 'Conferma NON armata: nessun indicatore (lunga = annulla).' },
  // ---- Avatar delle sessioni (docs/AVATAR.md): ogni avatar in Home Goccia e nel Dettaglio, pose diverse ----
  { name: 'avatar-demone-home-permesso', steps: [BOOT], note: 'Home Goccia: web-app (Demone) chiede un permesso (posa permission, nuvoletta «!»).' },
  { name: 'avatar-demone-dettaglio-permesso', steps: [BOOT, '2x'], note: 'Dettaglio di web-app: Demone a scala 3 in posa permission.' },
  { name: 'avatar-calamaro-home-lavora', steps: [BOOT, '1x'], note: 'Home Goccia con firmware-esp32 (Calamaro) che lavora.' },
  { name: 'avatar-calamaro-dettaglio-lavora', steps: [BOOT, '1x', '2x'], note: 'Dettaglio di firmware-esp32: Calamaro che lavora.' },
  { name: 'avatar-calamaro-home-aria', steps: [BOOT, 'sim:air', 'wait:2700', '1x'], note: 'CO2 a 1250 ppm: Calamaro in posa stuffy (onde d aria).' },
  { name: 'avatar-gatto-home-lavora', steps: [BOOT, '1x', '1x', '1x'], note: 'Home Goccia con api-server (Gatto) che lavora.' },
  { name: 'avatar-gatto-dettaglio-lavora', steps: [BOOT, '1x', '1x', '1x', '2x'], note: 'Dettaglio di api-server: Gatto che lavora.' },
  { name: 'avatar-gatto-home-stanco', steps: [BOOT, 'sim:limit', 'wait:2700', '1x', '1x', '1x'], note: 'Limite 5h al 92%: Gatto di api-server in posa tired.' },
  { name: 'avatar-predefinito-home-finito', steps: [BOOT, '1x', '1x'], note: 'docs-site non ha un avatar: Goccia (il predefinito) contenta.' },
  { name: 'avatar-gatto-home-finito', steps: [BOOT, '1x', '1x', 'sim:avatar'], note: 'Simula «Avatar della sessione» su docs-site: Gatto in posa done (salto).' },
  { name: 'avatar-demone-home-finito', steps: [BOOT, '1x', '1x', 'sim:avatar', 'sim:avatar'], note: 'docs-site con il Demone, posa done.' },
  { name: 'avatar-calamaro-home-finito', steps: [BOOT, '1x', '1x', ...times(3, 'sim:avatar')], note: 'docs-site con il Calamaro, posa done.' },
  { name: 'avatar-di-nuovo-predefinito', steps: [BOOT, '1x', '1x', ...times(4, 'sim:avatar')], note: 'Quarta pressione: docs-site torna all avatar predefinito (Goccia).' },
  { name: 'avatar-gatto-dettaglio-finito', steps: [BOOT, '1x', '1x', '2x', 'sim:avatar'], note: 'Dettaglio di docs-site con il Gatto, posa done.' },
  { name: 'avatar-demone-dettaglio-finito', steps: [BOOT, '1x', '1x', '2x', 'sim:avatar', 'sim:avatar'], note: 'Dettaglio di docs-site con il Demone.' },
  { name: 'avatar-calamaro-dettaglio-finito', steps: [BOOT, '1x', '1x', '2x', ...times(3, 'sim:avatar')], note: 'Dettaglio di docs-site con il Calamaro.' },
  { name: 'avatar-una-calamaro', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '2x', '3x', 'sim:avatar'], note: 'Una alla volta: web-app passa da Demone a Calamaro (scala 3, permission).' },
  { name: 'avatar-contatori-gatto', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x', ...times(3, 'sim:avatar')], note: 'Contatori: web-app con il Gatto a scala 1 (Demone -> Calamaro -> predefinito -> Gatto).' },
  // ---- Avatar predefinito (welcome/state del servizio: defaultAvatar): schermate senza sessione ----
  { name: 'avatar-predefinito-boot', steps: ['wait:500', 'sim:defavatar', 'wait:700'], note: 'Avvio con il Gatto come predefinito (posa working).' },
  { name: 'avatar-predefinito-boot-fine', steps: ['wait:500', 'sim:defavatar', 'sim:defavatar', 'wait:1700'], note: 'Fine dell avvio con il Demone (posa done).' },
  { name: 'avatar-predefinito-docs-site', steps: [BOOT, 'sim:defavatar', '1x', '1x'], note: 'Predefinito Gatto: docs-site (senza avatar) lo usa.' },
  { name: 'avatar-predefinito-riposo', steps: [BOOT, 'sim:defavatar', 'sim:defavatar', 'vlong'], note: 'Riposo con il Demone in pausa.' },
  { name: 'avatar-predefinito-offline', steps: [BOOT, ...times(3, 'sim:defavatar'), 'sim:pc', 'wait:2700'], note: 'PC non raggiungibile con il Calamaro in pausa.' },
  { name: 'avatar-predefinito-pc-spento', steps: [BOOT, 'sim:defavatar', 'sim:pcoff'], note: 'PC spento (riepilogo) con il Gatto fermo sul fotogramma 0.' },
  { name: 'avatar-predefinito-vuota', steps: [BOOT, 'sim:defavatar', 'sim:empty'], note: 'Nessuna sessione: Gatto in pausa nella Home.' },
  { name: 'avatar-predefinito-ricerca', steps: [BOOT, 'sim:defavatar', ...menuItem(3), ...times(4, '1x'), '2x'], note: 'Reti vicine con il Gatto a scala 2, centrato con w = 28.' },
  { name: 'avatar-predefinito-goccia', steps: [BOOT, ...times(4, 'sim:defavatar'), '1x', '1x'], note: 'Quarta pressione: il predefinito torna la Goccia.' },
  // ---- M6: energia, batteria scarica, Riposo con e senza cavo, calibrazione della batteria ----
  { name: 'riposo-usb', steps: [BOOT, 'sim:usb', 'wait:2700', 'vlong'], note: 'Riposo con il cavo: niente sonno profondo, «COL CAVO RESTO ACCESO».' },
  { name: 'riposo-senza-ora', steps: [BOOT, 'sim:noclock', 'vlong'], note: '0.10.1: Riposo senza l ora vera (accensione a freddo senza welcome né SNTP): «--:--» e niente data.' },
  { name: 'home-senza-ora', steps: [BOOT, 'sim:noclock'], note: '0.10.1: Home Goccia senza l ora vera: «--:--» accanto alla batteria.' },
  { name: 'menu-senza-ora', steps: [BOOT, 'sim:noclock', '3x'], note: '0.10.1: striscia del Menu senza l ora vera: «--:--».' },
  { name: 'offline-senza-ora', steps: [BOOT, 'sim:noclock', 'sim:pc', 'wait:2700'], note: '0.10.1: PC non raggiungibile senza l ora vera: «Ultimo dato alle --:--».' },
  { name: 'riposo-ora-arrivata', steps: [BOOT, 'sim:noclock', 'vlong', 'sim:noclock'], note: '0.10.1: l ora vera arriva nel Riposo: l orologio torna subito.' },
  { name: 'batteria-scarica', steps: [BOOT, 'sim:battcrit'], note: 'Simula «Batteria scarica» (sotto 3,4 V senza cavo): ultima schermata prima del sonno profondo senza timer.' },
  { name: 'batteria-scarica-tasto', steps: [BOOT, 'sim:battcrit', '2x'], note: 'Un tasto senza cavo: resta «Batteria scarica».' },
  { name: 'menu-batteria', steps: [BOOT, ...menuItem(9)], note: 'Menu > Batteria (sottomenu): Livello, Calibra la batteria «mai», Nuova batteria.' },
  { name: 'calibra-sotto-90', steps: [BOOT, ...menuItem(9), '1x', '2x'], note: 'Calibra con la batteria al 78%: toast «Carica almeno al 90%».' },
  { name: 'calibra-cavo', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'wait:2700', ...menuItem(9), '1x', '2x'], note: 'Calibra con il cavo: «Stacca il cavo», la prova parte da sola.' },
  { name: 'calibra-fase1', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x'], note: 'Calibrazione appena partita (100%): fase 1/2, massimo consumo, %/h in corso.' },
  { name: 'calibra-fase1-10min', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:600000'], note: 'Dopo 10 minuti: tensione e consumo misurati finora.' },
  { name: 'calibra-pausa', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:120000', 'sim:usb', 'wait:2700'], note: 'Cavo ricollegato durante la prova: «In pausa: stacca il cavo», il tempo non conta.' },
  { name: 'calibra-annullata', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:120000', '3x'], note: '3x durante la prova: «Calibrazione annullata», torna al sottomenu Batteria.' },
  { name: 'calibra-fase2', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:3720000'], note: 'Dopo 62 minuti: fase 2/2, attesa (schermo fermo, una lettura al minuto).' },
  { name: 'calibra-fatta', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:7230000'], note: 'Fine delle due fasi: consumo in Attivo e in Attesa.' },
  { name: 'calibra-fatta-indietro', steps: [BOOT, 'sim:battlvl', ...menuItem(9), '1x', '2x', 'wait:7230000', 'long'], note: 'Pressione lunga dopo la calibrazione: sottomenu Batteria con la data dell ultima calibrazione.' },
  { name: 'menu-batteria-calibrata', steps: [BOOT, 'sim:calsaved', ...menuItem(9)], note: 'Simula «Batteria calibrata» (8 %/h e 1,5 %/h): data nel sottomenu.' },
  { name: 'batteria-calibrata', steps: [BOOT, 'sim:calsaved', ...menuItem(9), '2x'], note: 'Livello con la calibrazione: «Autonomia: circa N giorni con il tuo uso» (uso tipico 30/50/20).' },
  { name: 'batteria-calibrata-usb', steps: [BOOT, 'sim:calsaved', 'sim:usb', 'wait:2700', ...menuItem(9), '2x'], note: 'Con il cavo la stima resta quella della calibrazione.' },
  { name: 'nuova-batteria-conferma', steps: [BOOT, ...menuItem(9), '1x', '1x', '2x'], note: 'Nuova batteria premuta una volta: «conferma?» e toast.' },
  { name: 'nuova-batteria', steps: [BOOT, ...menuItem(9), '1x', '1x', '2x', '2x'], note: 'Seconda doppia pressione: calibrazione cancellata, selezione su Calibra la batteria.' },
  { name: 'info-cavo-usb', steps: [BOOT, ...menuItem(12), ...times(8, '1x')], note: 'Info > Cavo USB senza cavo: «no (0/8)» (diagnosi del rilevamento, a batteria non c è la seriale).' },
  { name: 'info-cavo-usb-si', steps: [BOOT, 'sim:usb', 'wait:2700', ...menuItem(12), ...times(8, '1x')], note: 'Info > Cavo USB con il cavo: «sì (8/8)».' },
  { name: 'dimentica-scaduta', steps: [BOOT, ...menuItem(3), ...times(8, '1x'), '2x', 'wait:6200'], note: 'Senza conferma entro 6 s la voce torna normale.' },
  // Aggiornamento remoto (docs/OTA.md §1.6): sim:ota avanza di una fase per pressione; ogni passo è un ridisegno e la
  // corsa (posa run, AVATAR.md §12) avanza di un fotogramma.
  { name: 'ota-0', steps: [BOOT, ...times(1, 'sim:ota')], note: 'AGGIORNAMENTO 0.6.0 → 0.7.0 a 0%: la Goccia ferma al bordo sinistro, fotogramma 0 della corsa.' },
  { name: 'ota-37', steps: [BOOT, ...times(2, 'sim:ota')], note: 'Ricezione al 37%: il bordo destro della figura sul fronte della barra, fotogramma 1.' },
  { name: 'ota-80', steps: [BOOT, ...times(3, 'sim:ota')], note: 'Ricezione all 80%, fotogramma 0 (profilo ridotto: k mod 2).' },
  { name: 'ota-verifica', steps: [BOOT, ...times(4, 'sim:ota')], note: 'Verifica: barra piena, la figura alla fine.' },
  { name: 'ota-riavvio', steps: [BOOT, ...times(5, 'sim:ota')], note: 'Fatto: posa FINITO alla fine della barra e «Riavvio…».' },
  { name: 'ota-errore', steps: [BOOT, ...times(6, 'sim:ota')], note: 'Errore: posa ERRORE, «NON RIUSCITO», resta la versione di prima, motivo.' },
  { name: 'ota-rifiutato', steps: [BOOT, ...times(7, 'sim:ota')], note: 'Rifiutato con il motivo (batteria al 42%).' },
  { name: 'ota-rifiutato-chiuso', steps: [BOOT, ...times(7, 'sim:ota'), '1x'], note: 'Un gesto chiude la schermata del rifiuto: torna la Home.' },
  { name: 'ota-37-tasto', steps: [BOOT, ...times(2, 'sim:ota'), '3x'], note: 'Durante la ricezione i gesti non contano (3x non apre il Menu).' },
  { name: 'ota-37-demone', steps: [BOOT, 'sim:defavatar', 'sim:defavatar', ...times(2, 'sim:ota')], note: 'Avatar predefinito Demone: corre specchiato verso destra.' },
  { name: 'ota-0-gatto', steps: [BOOT, 'sim:defavatar', ...times(1, 'sim:ota')], note: 'Avatar predefinito Gatto a 0%.' },
  // ---- Giornata di lavoro (0.9.0): Menu › Strumenti › Giornata di lavoro (voce 6), comando `workday` del PC (sim:work),
  // ultimi minuti (sim:worklast: fine fra 25 minuti) e fine (sim:workend: la giornata finisce al prossimo tick). Orologio
  // finto alle 10:30: con la fine alle 18:00 mancano 7 h 30, la barra parte dalle 9:00.
  { name: 'menu-strumenti-giornata-voce', steps: [BOOT, ...menuItem(1), ...times(5, '1x')], note: 'Strumenti con 6 voci: Giornata di lavoro «spenta» selezionata (la lista scorre).' },
  { name: 'giornata-menu', steps: [BOOT, ...menuItem(1), ...times(5, '1x'), '2x'], note: 'Sottomenu Giornata di lavoro: Inizia ora, Ferma, Inizio 09:00, Fine 18:00.' },
  { name: 'giornata-avviata', steps: [BOOT, ...menuItem(1), ...times(5, '1x'), '2x', '2x'], note: 'Inizia ora (2×): toast «Giornata fino alle 18:00», valore «attiva».' },
  { name: 'giornata-strumenti-attiva', steps: [BOOT, ...menuItem(1), ...times(5, '1x'), '2x', '2x', 'wait:2700', 'long'], note: 'Indietro a Strumenti: «fino alle 18:00».' },
  { name: 'giornata-fermata', steps: [BOOT, ...menuItem(1), ...times(5, '1x'), '2x', '2x', 'wait:2700', '1x', '2x'], note: 'Ferma (2×): toast «Giornata fermata».' },
  { name: 'riposo-giornata', steps: [BOOT, 'sim:work', 'wait:2700', 'vlong'], note: 'Riposo con la giornata attiva: MANCANO 7 H 30, fino alle 18:00, barra dalle 9:00 alle 18:00, mascotte più in basso.' },
  { name: 'riposo-giornata-usb', steps: [BOOT, 'sim:usb', 'sim:work', 'wait:2700', 'vlong'], note: 'Come sopra con il cavo: «COL CAVO RESTO ACCESO».' },
  { name: 'home-goccia-giornata', steps: [BOOT, 'sim:work', 'wait:2700'], note: 'Home Goccia: «7 h 30» a sinistra della batteria, sulla riga dell orologio.' },
  { name: 'home-goccia-giornata-avvisi', steps: [BOOT, 'sim:work', 'long', 'wait:2700'], note: 'Con la campanella barrata (silenzio): la riga si sposta a sinistra e ci sta ancora.' },
  { name: 'home-goccia-giornata-pieno', steps: [BOOT, 'sim:battlvl', 'sim:usb', 'sim:work', 'long', ...menuItem(5), '1x', '2x', '3x', 'wait:2700'], note: 'Caso più largo: 100%, fulmine, campanella e luna: la riga non ci sta e non si disegna.' },
  { name: 'home-contatori-giornata', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x', 'sim:work', 'wait:2700'], note: 'Contatori: «7 h 30» prima della batteria, il nome si accorcia.' },
  { name: 'home-righe-giornata', steps: [BOOT, ...TO_DISPLAY, '2x', '3x', 'sim:work', 'wait:2700'], note: 'Righe: niente orologio, niente riga della giornata.' },
  { name: 'riposo-giornata-ultimi', steps: [BOOT, 'sim:worklast', 'wait:2700', 'vlong'], note: 'Ultimi minuti: MANCANO 25 MIN, fino alle 10:55, barra quasi piena.' },
  { name: 'home-goccia-giornata-ultimi', steps: [BOOT, 'sim:worklast', 'wait:2700'], note: 'Home Goccia con «25 min».' },
  { name: 'giornata-finita-toast', steps: [BOOT, '1x', '1x', 'sim:workend', 'wait:200'], note: 'Fine della giornata sulla Home (docs-site selezionata, FINITO): toast «GIORNATA FINITA», festa (mascotte contenta), riga «finita».' },
  { name: 'giornata-finita-home', steps: [BOOT, 'sim:workend', 'wait:5300'], note: 'Toast scaduto: resta la riga «finita» accanto all orologio (un ora).' },
  { name: 'riposo-giornata-finita', steps: [BOOT, 'sim:workend', 'wait:200', 'vlong'], note: 'Riposo dopo la fine: GIORNATA / FINITA / alle 10:30, barra piena, mascotte contenta.' },
  { name: 'giornata-finita-spenta', steps: [BOOT, 'sim:workend', 'wait:200', ...menuItem(1), ...times(5, '1x')], note: 'Dopo la fine la voce torna «spenta» (si spegne da sola).' },
  // ---- Gioco «Alto o basso» (0.9.0): Menu › Gioco › Alto o basso (0.11.0; prima Menu › Strumenti, voce 7). Seme fisso: carte 2, 10, J, 10, 7, K, 3, 2, 8, J…
  { name: 'gioco-inizio', steps: [BOOT, ...menuItem(2), '2x'], note: 'Inizio partita: carta 2, SERIE 0, record 0, «Più alta o più bassa?», mascotte che chiede.' },
  { name: 'gioco-giusto', steps: [BOOT, ...menuItem(2), '2x', '1x'], note: '1× (più alta): 2 → 10, GIUSTO, serie 1, mascotte contenta.' },
  { name: 'gioco-serie-3', steps: [BOOT, ...menuItem(2), '2x', '1x', '1x', '2x'], note: 'Più alta, più alta, più bassa: 2 → 10 → J → 10, serie 3, record 3.' },
  { name: 'gioco-sconfitta-record', steps: [BOOT, ...menuItem(2), '2x', '1x', '1x', '2x', '1x'], note: 'Più alta su 10 → 7: SBAGLIATO, FINE · SERIE 3, NUOVO RECORD!, mascotte che sbaglia.' },
  { name: 'gioco-nuova-partita', steps: [BOOT, ...menuItem(2), '2x', '1x', '1x', '2x', '1x', '2x'], note: '2× dopo la sconfitta: nuova partita con la K, record 3.' },
  { name: 'gioco-sconfitta', steps: [BOOT, ...menuItem(2), '2x', '1x', '1x', '2x', '1x', '2x', '1x'], note: 'Più alta sulla K: SBAGLIATO, serie 0, record 3.' },
  { name: 'gioco-record-menu', steps: [BOOT, ...menuItem(2), '2x', '1x', '1x', '2x', '1x', 'long'], note: 'Pressione lunga: esce, la voce dice «record 3».' },
  { name: 'gioco-permesso', steps: [BOOT, ...menuItem(2), '2x', '1x', 'sim:perm'], note: 'Un permesso in arrivo interrompe la partita: Home con il toast «Partita interrotta: un permesso».' },
  { name: 'gioco-tieni', steps: [BOOT, ...menuItem(2), '2x', 'down', 'wait:1500'], note: 'Nel gioco il tenuto 3 s resta il Riposo (indicatore a metà).' },
  // ---- Gioco della mascotte (0.10.0): sim:game (impostazione remota `game`), sim:petlevel (sale di livello), sim:petgoal
  // (traguardo: Primo task, Primo verde, 10 task, 7 giorni di serie, Livello 5, a giro), sim:petwear (indossa: nessuno →
  // cappellino, occhiali, sciarpa, papillon, corona, cuffie, fiocco, mago → nessuno), sim:pettired (stanca / riposata),
  // sim:petspecial (accessorio speciale da chiedere a Claude). Il gioco parte spento anche sul PC (pet.on): sim:game lo accende.
  // Riassunto finto: livello 3, 66/417 XP, energia 80%, cappellino sbloccato. Con il gioco acceso «La mascotte» è la voce 2
  // del Menu. La sessione web-app chiede un permesso: la festa si vede solo dopo sim:elsewhere.
  { name: 'gioco-menu-display', steps: [BOOT, ...TO_DISPLAY, ...times(8, '1x')], note: 'Menu › Display, penultima voce: Gioco della mascotte «no» (spento di default).' },
  { name: 'gioco-acceso-menu', steps: [BOOT, ...TO_DISPLAY, ...times(8, '1x'), '2x'], note: '2× sulla voce: «sì».' },
  { name: 'menu-tema', steps: [BOOT, ...TO_DISPLAY, ...times(9, '1x')], note: 'Menu › Display, ultima voce: Tema «Calamaio» (0.13.0).' },
  { name: 'menu-tema-lista', steps: [BOOT, ...TO_DISPLAY, ...times(9, '1x'), '2x'], note: 'Display › Tema: solo «Calamaio» con ✓.' },
  { name: 'gioco-menu-mascotte', steps: [BOOT, 'sim:game', '3x', '1x', '1x'], note: 'Menu con il gioco acceso: «La mascotte» (livello 3) dopo Strumenti.' },
  { name: 'mascotte', steps: [BOOT, 'sim:game', ...menuItem(2)], note: 'La mascotte: livello 3, barra e 66/417 XP, energia 80%, nessun traguardo; Goccia contenta senza accessori.' },
  { name: 'mascotte-traguardi', steps: [BOOT, 'sim:game', ...times(4, 'sim:petgoal'), 'wait:3200', ...menuItem(2)], note: 'Quattro traguardi (con il permesso aperto: solo toast): gli ultimi 3, il più recente in alto.' },
  { name: 'mascotte-accessorio', steps: [BOOT, 'sim:game', ...menuItem(2), '2x'], note: 'Accessorio: Nessuno (indossato), Cappellino; selezione sull indossato.' },
  { name: 'mascotte-indossa', steps: [BOOT, 'sim:game', ...menuItem(2), '2x', '1x', '2x'], note: '1× Cappellino, 2× indossa: torna a La mascotte con il cappellino e il toast «Indossa: Cappellino».' },
  { name: 'mascotte-togli', steps: [BOOT, 'sim:game', 'sim:petwear', ...menuItem(2), '2x', '1x', '2x'], note: 'Con il cappellino: 1× va su Nessuno, 2× lo toglie («Nessun accessorio»).' },
  { name: 'mascotte-livello', steps: [BOOT, 'sim:game', 'sim:petlevel', ...menuItem(2)], note: 'Livello 4 (pari): sbloccati gli occhiali; 25/606 XP.' },
  { name: 'mascotte-livello-accessori', steps: [BOOT, 'sim:game', 'sim:petlevel', ...menuItem(2), '2x'], note: 'Accessorio con Nessuno, Cappellino, Occhiali.' },
  { name: 'mascotte-stanca', steps: [BOOT, 'sim:game', 'sim:pettired', ...menuItem(2)], note: 'Stanca: posa stanca, «stanca 15%».' },
  { name: 'mascotte-spenta-dal-pc', steps: [BOOT, 'sim:game', ...menuItem(2), '2x', 'sim:game'], note: 'Il PC spegne il gioco mentre si sceglie l accessorio: si torna al Menu, senza «La mascotte».' },
  { name: 'gioco-home-goccia', steps: [BOOT, 'sim:game', 'sim:petwear'], note: 'Home Goccia: il Demone (permesso) con il cappellino.' },
  { name: 'gioco-home-goccia-occhiali', steps: [BOOT, 'sim:game', ...times(2, 'sim:petwear'), '1x', '1x'], note: 'docs-site (Goccia, finito): occhiali.' },
  { name: 'gioco-home-gatto-cuffie', steps: [BOOT, 'sim:game', ...times(6, 'sim:petwear'), '1x', '1x', '1x'], note: 'api-server (Gatto, lavora): cuffie con i padiglioni sulle orecchie.' },
  { name: 'gioco-home-mago', steps: [BOOT, 'sim:game', ...times(8, 'sim:petwear'), '1x', '1x'], note: 'docs-site (Goccia, finito): berretto da mago calato in testa.' },
  { name: 'gioco-dettaglio', steps: [BOOT, 'sim:game', ...times(3, 'sim:petwear'), '1x', '2x'], note: 'Dettaglio di firmware-esp32 (Calamaro, lavora): sciarpa a 2 fotogrammi.' },
  { name: 'gioco-dettaglio-fiocco', steps: [BOOT, 'sim:game', ...times(7, 'sim:petwear'), '1x', '1x', '1x', '2x'], note: 'Dettaglio di api-server (Gatto): fiocco.' },
  { name: 'gioco-home-una', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '2x', '3x', 'sim:game', ...times(5, 'sim:petwear')], note: 'Una alla volta: il Demone (permesso) con la corona bassa.' },
  { name: 'gioco-home-contatori', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x', 'sim:game', ...times(4, 'sim:petwear')], note: 'Contatori (scala 1): papillon con il filo bianco.' },
  { name: 'gioco-stanca-home', steps: [BOOT, 'sim:game', 'sim:pettired', '1x'], note: 'Gioco: stanca. firmware-esp32 (lavora) in posa stanca, come con i limiti.' },
  { name: 'gioco-festa', steps: [BOOT, 'sim:game', 'sim:petwear', 'sim:elsewhere', 'sim:petgoal'], note: 'Traguardo senza permessi aperti: festa di 3 s (posa finito, coriandoli e scintille) e toast «Traguardo: Primo task».' },
  { name: 'gioco-festa-fine', steps: [BOOT, 'sim:game', 'sim:petwear', 'sim:elsewhere', 'sim:petgoal', 'wait:3200'], note: 'Dopo 3 s la festa finisce: la mascotte torna a lavorare.' },
  { name: 'gioco-festa-contatori', steps: [BOOT, ...TO_DISPLAY, '2x', '2x', '3x', 'sim:game', 'sim:elsewhere', 'sim:petgoal'], note: 'Festa in Contatori (scala 1).' },
  { name: 'gioco-traguardo-permesso', steps: [BOOT, 'sim:game', 'sim:petgoal'], note: 'Permesso aperto: niente festa, solo il toast.' },
  { name: 'gioco-traguardo-silenzio', steps: [BOOT, 'sim:elsewhere', 'long', 'wait:2700', 'sim:game', 'sim:petgoal'], note: 'In silenzio: solo il toast.' },
  { name: 'gioco-traguardo-notturno', steps: [BOOT, 'sim:elsewhere', ...menuItem(5), '1x', '2x', '3x', 'sim:game', 'sim:petgoal'], note: 'Notturno: solo il toast.' },
  { name: 'gioco-traguardo-risparmio', steps: [BOOT, 'sim:elsewhere', ...TO_DISPLAY, '1x', '1x', '1x', '2x', '3x', 'sim:game', 'sim:petgoal'], note: 'Risparmio energetico: solo il toast.' },
  { name: 'gioco-spento', steps: [BOOT, 'sim:game', 'sim:petwear', 'sim:pettired', 'sim:game'], note: 'Gioco rispento dal PC: niente accessorio, niente posa stanca (uguale a home-goccia).' },
  { name: 'gioco-spento-traguardo', steps: [BOOT, 'sim:elsewhere', 'sim:petgoal'], note: 'Gioco spento: un traguardo non fa niente (niente toast, niente festa).' },
  { name: 'gioco-speciale-toast', steps: [BOOT, 'sim:game', 'sim:petspecial'], note: 'Accessorio speciale da chiedere (game.special): toast «Nuovo accessorio speciale: chiedilo a Claude», una volta per accensione.' },
  { name: 'mascotte-speciale', steps: [BOOT, 'sim:game', ...times(2, 'sim:petgoal'), 'sim:petspecial', 'wait:5200', ...menuItem(2)], note: 'La mascotte con lo speciale: due traguardi e la riga «Speciale: chiedilo a Claude».' },
  { name: 'gioco-ota', steps: [BOOT, 'sim:game', 'sim:petwear', 'sim:ota', 'sim:ota'], note: 'Aggiornamento: la Goccia corre con il cappellino.' }
];
