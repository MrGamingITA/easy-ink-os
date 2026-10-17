// Comandi dell'interfaccia: porting della parte «componente» del simulatore
// (act, confirmPerm, activate, doAct, tick, slowTick, step, raise, finishConnect, gesture).
//
// Ogni metodo pubblico riceve il tempo dall'esterno e restituisce il refresh che il pannello
// deve fare dopo il comando (RefreshKind). Il simulatore lo decide così:
//   - redraw('partial'): piccoli cambi dentro la stessa schermata (selezione, toast, valori);
//   - redraw('quick'):   cambio di schermata (push/pop della pila, avvio, riposo, inverti, ruota);
//   - scr.full():        solo Display › Refresh completo;
//   - fotogrammi delle animazioni in tick(): parziali.
// Più richieste nello stesso comando si fondono come redraw(): vince la più forte
// (None < Partial < Quick < Full). Il conteggio «completo ogni N parziali» è di RefreshCounter.
#pragma once
#include <stdint.h>

#include "game.h"
#include "gesture.h"
#include "menu.h"
#include "model.h"
#include "pad.h"

namespace cal {

enum class RefreshKind : uint8_t { None, Partial, Quick, Full };
inline RefreshKind mergeRefresh(RefreshKind a, RefreshKind b) { return a > b ? a : b; }
const char *refreshKey(RefreshKind k);  // 'none', 'partial', 'quick', 'full'

// Contatore dei parziali, come class Screen: partial() conta e dopo fullEvery fa un completo;
// quick() conta senza controllare la soglia; full() azzera.
class RefreshCounter {
 public:
  explicit RefreshCounter(int fullEvery) : fullEvery_(fullEvery), partials_(0) {}
  // Trasforma la richiesta nel refresh da eseguire davvero sul pannello.
  RefreshKind apply(RefreshKind req);
  int partials() const { return partials_; }
  // 0.10.1: dopo il sonno profondo si continua a contare dai parziali salvati nella memoria RTC.
  void restore(int partials) { partials_ = partials < 0 ? 0 : partials >= fullEvery_ ? fullEvery_ - 1 : partials; }

 private:
  int fullEvery_;
  int partials_;
};

// Azioni del simulatore (act(name)). Prev/Home/Mute servono a rotella e tre tasti (M7).
enum class Action : uint8_t { Next, Prev, Ok, Menu, Back, Sleep, Home, Mute };

// Sorgente di casualità per step()/slowTick() (il simulatore usa Math.random).
// xorshift32: nei test lo stesso seme riproduce la sequenza del golden JS.
class Rng {
 public:
  explicit Rng(uint32_t seed) : s_(seed ? seed : 0x9E3779B9u) {}
  double next();  // [0, 1)

 private:
  uint32_t s_;
};

// Punto di uscita dei permessi. Senza rete la risposta resta locale (si aggiorna lo stato finto come fa
// il simulatore); con la rete il loop la passa al task di rete, che manda perm-reply firmato (M5 §3).
// Solo Allow e Deny sono risposte da inviare: Cancelled è informativo (pressione lunga: la richiesta resta
// aperta, qui e nel terminale; non c'è più una scadenza, vedi DECISIONI «Permessi senza scadenza»).
enum class PermOutcome : uint8_t { Allow, Deny, Cancelled };
// 0.14.0: da dove viene il gesto che risponde (spec app iPhone §2.3, scelta A): il telefono non dà mai CONSENTI.
enum class InputSrc : uint8_t { Prg, Xbox, Phone };
constexpr const char *kPhoneAllowToast = "Per consentire usa il tasto PRG o il controller";
// R1 e F11: Proteggi, Ripristino, abbinamento col PC e Wi-Fi mai dal telefono.
constexpr const char *kPhoneConfirmToast = "Per confermare usa il tasto PRG o il controller";
// R1, F11, regola 3: gesto del PRG o dell'Xbox annullato perché il telefono ha cambiato schermata, scelta o voce.
constexpr const char *kPhoneRaceToast = "Il telefono ha cambiato la schermata: ripeti";
// Regola 3, margine: un cambio del telefono fino a 2 s prima della prima pressione annulla anche lui il gesto
// (parziale del pannello ~650 ms più il tempo di reazione: lo schermo può mostrare ancora la scelta di prima).
constexpr Ms kPhoneSettleMs = 2000;
struct PermEvent {
  PermOutcome outcome;
  int session;            // indice nel modello
  const char *requestId;  // id di perm-request ('' senza rete)
  const char *sessionName;
  Ms at;
  InputSrc src;  // 0.14.0: onPerm scarta un Allow dal telefono (terza barriera)
};
typedef void (*PermListener)(const PermEvent &e, void *ctx);

// M3: richieste dell'interfaccia alla rete vera (solo con sys.live). Le esegue il task di rete.
enum class NetCmd : uint8_t {
  Scan,             // Cerca reti
  Provision,        // Configura da telefono (o rete protetta scelta in Cerca reti): AP + portale
  CancelProvision,  // l'utente ha lasciato la schermata del QR
  Join,             // rete aperta scelta in Cerca reti: collegati senza password
  Forget,           // Dimentica tutte le reti (già confermata): vault, pulizia profonda e riavvio
  Reboot,           // Info › Riavvia
  Settings,         // impostazioni remote cambiate (o dopo un set): la rete le tiene e manda `settings`
  Ack,              // esito di set o full-refresh (id, ok, error)
  Dismiss,          // Archivia: dismiss {id, session} al servizio
  // M5
  PairConfirm,      // 2× sulla schermata Abbinamento dopo il codice giusto: pair-done
  PairCancel,       // pressione lunga sulla schermata Abbinamento: pair-abort {reason: display}
  PermReply,        // Consenti o Nega confermato: perm-reply (id, ok = consenti)
  Unpair,           // Dimentica abbinamento (già confermata): unpair e K cancellata
  ProtectKeys,      // Proteggi le chiavi (2× e tieni 3 s): eFuse, la esegue il loop
  FactoryReset,     // Ripristino di fabbrica (2× e tieni 3 s): cancella e riavvia, la esegue il loop
  AvatarNeed,       // avatar-need {ids}: avatar che il display non ha (core/avatars, AvatarNeeds)
  OtaGo,            // risposta del loop a un ota-offer già verificato: ok = parte (condizioni del §1), error = motivo
  Status,           // status {batt, usb, cal} al servizio (PROTOCOLLO.md «OTA»): power
  ForgetNet,        // 0.8.0: Reti salvate › una rete › Dimentica (già confermata): ssid; pulizia profonda e riavvio
  // 0.10.0, gioco della mascotte (core/pet, core/accessories)
  AccWear,          // La mascotte › Accessorio › 2×: accessory-wear {id | null}; id in avatarIds[0] ('' = nessuno)
  AccNeed,          // accessory-need {ids}: avatarIds, nAvatarIds
  AnchorsNeed,      // anchors-need {avatars: [{id, crc}]}: avatarIds, avatarCrcs, nAvatarIds
  ThemeCur,         // 0.13.0: tema attivo cambiato (id = tema, '' = di serie): la rete manda themes
  AvatarAck,        // 0.14.0 (R4): avatar scaricato e adottato (o no) dalla cache: id, avatarCrcs[0], ok, error
};
const char *netCmdKey(NetCmd c);
const int kNetIdLen = 33;   // = kCmdIdLen di core/remote_settings.h
const int kNetErrLen = 96;  // = kAckErrLen
struct NetRequest {
  NetCmd cmd;
  char ssid[kSsidLen];  // Provision: rete preselezionata nel portale ('' = nessuna); Join, ForgetNet: rete
  RemoteValues values;  // Settings
  char id[kNetIdLen];   // Ack; PermReply: id della richiesta
  bool ok;              // Ack; PermReply: true = consenti
  char error[kNetErrLen];  // Ack con ok = false
  char session[kIdLen];    // Dismiss: session_id (id = id della richiesta)
  char avatarIds[kMaxSessions + 1][kAvatarRefLen];  // AvatarNeed: sessioni + predefinito; AccWear, AccNeed, AnchorsNeed
  uint32_t avatarCrcs[kMaxSessions + 1];             // AnchorsNeed: CRC del CAVT di ciascun avatar
  uint8_t nAvatarIds;
  int8_t battPct;  // Status: 0–100, −1 = non si sa
  int8_t usbOn;    // Status: 1, 0, −1 = non si sa
  bool cal;        // Status: calibrazione in corso
  InputSrc src;          // R1, F11: onNetRequest scarta dal telefono i comandi di phoneForbidden
  bool hasWork, workOn;  // Status (0.9.0): workday {on, end}; hasWork sempre vero dal firmware 0.9.0
  int64_t workEnd;       // Status: fine in ms epoch (con workOn)
};
typedef void (*NetListener)(const NetRequest &r, void *ctx);
// R1 e F11 (spec app iPhone §0): comandi che dal telefono non partono mai. eFuse, Ripristino, abbinamento della scheda
// col PC (conferma e «Dimentica abbinamento») e Wi-Fi (configurazione, rete aperta, dimentica). Riavvia resta ammesso.
// Seconda barriera in Ui::emitNet, terza in onNetRequest (main.cpp).
bool phoneForbidden(NetCmd c);
// Terza barriera (onPerm e onNetRequest in main.cpp): true = dal telefono, si scarta. Vale anche se Ui fosse aggirata:
// CONSENTI (anche come perm-reply con ok) e i comandi di phoneForbidden.
bool dropFromPhone(const PermEvent &e);
bool dropFromPhone(const NetRequest &r);

struct StateMsg;  // core/apply_state.h
struct GameMsg;   // core/pet.h

// 0.11.0: richieste di Menu › Gioco › Controller al Bluetooth (hw/blepad), lette una volta dal loop.
enum class PadReq : uint8_t { None, Pair, Forget, PairPhone, ForgetPhone, CancelPair };

class Ui {
 public:
  explicit Ui(const DeviceProfile &dev);

  // newModel + pila con l'avvio. Restituisce Full: sul pannello vero il primo disegno è un completo.
  RefreshKind begin(Ms now, const WallClock &wc);

  // Gesto riconosciuto da GestureRecognizer (un tasto solo): gesture() del simulatore. VeryLong con il
  // tasto ancora giù (poll() a 3 s) è holdFire(): azione subito e «FATTO · RILASCIA».
  RefreshKind gesture(Gesture g, Ms now, const WallClock &wc);
  // keyDown()/keyUp() del simulatore per l'indicatore del tenuto 3 s. keyUp() prima del gesto del rilascio.
  RefreshKind keyDown(Ms now);
  RefreshKind keyUp(Ms now);
  // act(name, homeMute) del simulatore.
  RefreshKind act(Action a, Ms now, const WallClock &wc, bool homeMute = false);
  // Da chiamare spesso (il simulatore ogni 100 ms): scadenze, toast, minuto, animazioni.
  RefreshKind tick(Ms now, const WallClock &wc);
  // Ogni 10 s: batteria (finché non arriva battery()), segnale e temperatura finti.
  RefreshKind slowTick(Ms now, Rng &rng);
  // Ogni 4,2 s: evoluzione delle sessioni finte (solo M2; in M3 lo stato arriva dal PC).
  RefreshKind step(Ms now, const WallClock &wc, Rng &rng);
  // raise(s, kind) del simulatore: la sessione passa a permission, input o error.
  RefreshKind raise(int session, SessionState kind, Ms now, Rng &rng);

  void setPermListener(PermListener fn, void *ctx) { permFn_ = fn; permCtx_ = ctx; }

  // ---------- M3: rete vera ----------
  // Dopo begin(): toglie i dati finti e accende sys.live. Da qui step() non fa niente e slowTick()
  // aggiorna solo la batteria finta finché non arriva battery(); i flussi Wi-Fi chiedono alla rete (NetListener) invece di simulare.
  void goLive();
  void setNetListener(NetListener fn, void *ctx) { netFn_ = fn; netCtx_ = ctx; }
  // Eventi dal task di rete, applicati nel loop. Restituiscono il refresh come gli altri comandi.
  RefreshKind netApReady(const char *apPass, Ms now);  // AP acceso: mostra il QR (lo apre se serve)
  RefreshKind netConnecting(const char *ssid, Ms now); // credenziali dal portale: «Connessione a …»
  RefreshKind netConnected(const char *ssid, int rssi, const char *ip, Ms now);
  RefreshKind netConnectFailed(const char *ssid, Ms now);
  RefreshKind netWifiLost(Ms now);
  RefreshKind netRssi(int rssi, Ms now);
  // M6: livello vero della batteria (hw/battery, ogni 60 s). Da qui slowTick() non simula più la scarica.
  RefreshKind battery(int pct, Ms now);
  // Cavo USB (hw/usb, stato stabile di cal::UsbFilter): come il pulsante Simula «USB in carica»,
  // toast «In carica» / «A batteria» e ridisegno. Nessun cambiamento → niente.
  RefreshKind usb(bool on, Ms now);
  RefreshKind netScanDone(const NetEntry *nets, int n, Ms now);
  // 0.8.0: reti salvate dal vault (ordine di inserimento, ultimo uso già in ora locale, visibilità dall'ultima
  // ricerca). Ridisegna solo se la lista è a schermo e qualcosa è cambiato.
  RefreshKind netSaved(const SavedNet *list, int n, Ms now);
  // wifi-forget dal PC (già verificato e già tolto dal vault): toast «Rete dimenticata»; la scheda riavvia fra 3 s.
  RefreshKind netForgotten(const char *ssid, Ms now);
  RefreshKind netPcHost(const char *host, Ms now);  // PC trovato via mDNS
  RefreshKind netPcLost(Ms now);                    // 45 s senza niente dal PC, o connessione chiusa
  RefreshKind netState(const StateMsg &msg, Ms now);
  // `set` dal PC (già controllato, core/remote_settings): applica le chiavi presenti come le voci del menu
  // Display e Avvisi, con lo stesso refresh (inverti e ruota → quick, il resto → partial). mute: Hour = un'ora
  // da adesso, Night = fino alle 8, None = toglie il silenzio. permOn arriva solo false.
  RefreshKind applyRemote(const SettingsPatch &p, Ms now, const WallClock &wc);
  // `ack` del servizio a un dismiss (Archivia). ok = false: la sessione torna con il toast «Non archiviata».
  // Un id sconosciuto o già scaduto si ignora (None).
  RefreshKind netAck(const char *id, bool ok, Ms now);
  bool provisioning() const { return provActive_; }

  // ---------- M5: abbinamento, autenticazione, permessi, chiavi ----------
  // Codice dal servizio (pair-reveal verificato): apre la schermata Abbinamento con il codice da scrivere sul PC.
  // Funziona anche senza rete: è il pulsante «Abbinamento dal PC» del simulatore.
  RefreshKind netPairCode(const char *code, Ms now);
  // pair-confirm valido: il codice era giusto, ora si conferma con 2× sul display.
  RefreshKind netPairReady(Ms now);
  // Abbinamento finito o interrotto dal servizio (codice sbagliato, tempo, connessione): toast «Abbinamento annullato».
  RefreshKind netPairAborted(const char *reason, Ms now);
  // K salvata dal task di rete (pair-done mandato): stato «abbinato a <host>».
  RefreshKind netPairDone(Ms now);
  RefreshKind netAuthed(Ms now);  // auth-ok valido
  RefreshKind netRedo(Ms now);    // chiavi diverse tra display e servizio: «da rifare»
  // perm-request verificata (core/link): in coda. epochNow = ora del muro in ms epoch (0 = sconosciuta):
  // con l'ora valida «IN ATTESA DA …» parte da `at`, altrimenti dall'arrivo.
  RefreshKind netPermRequest(const PermReq &r, Ms now, int64_t epochNow);
  // perm-close: via dalla coda; se era a schermo si chiude con «Risposto altrove».
  RefreshKind netPermClose(const char *id, Ms now);
  // Esito di «Proteggi le chiavi» (eseguito dal loop).
  RefreshKind netKeysProtected(bool ok, Ms now);

  // ---------- M6: energia e batteria ----------
  // `bye` del servizio: riepilogo «PC spento» (sys.pcOff) con refresh completo. Il sonno lo decide core/power.
  RefreshKind netBye(Ms now);
  // Batteria sotto la soglia critica senza cavo: schermata «Batteria scarica» (come Simula «Batteria scarica»).
  RefreshKind battLow(Ms now);
  // Calibrazione: con la rete la lettura dovuta (calibDue) la fa il loop con l'ADC e la passa qui; senza rete
  // (simulatore, test) la fa tick() dalla percentuale finta, come il simulatore.
  bool calibDue() const { return m_.calib.due; }
  RefreshKind calibSample(int mv, Ms now, const WallClock &wc);
  // Calibrazione finita o cancellata («Nuova batteria»): il loop salva m.bat.cal nella NVS. Si legge una volta.
  bool takeBatChanged() { const bool c = batChanged_; batChanged_ = false; return c; }

  // ---------- aggiornamento remoto (docs/OTA.md §1.6) ----------
  // Offerta accettata: schermata AGGIORNAMENTO a 0% (sopra tutto). Ridisegno completo rapido.
  RefreshKind otaStart(const char *from, const char *to, uint32_t total, Ms now);
  // Byte ricevuti: ridisegno (Partial) solo ogni ~5% e non prima di 2 s dall'ultimo; a ogni ridisegno la corsa
  // avanza di un fotogramma. Altrimenti None.
  RefreshKind otaProgress(uint32_t done, Ms now);
  // Verifica, scrittura, riavvio (posa «finito», «Riavvio…»), errore (si chiude da sola dopo kOtaResultMs).
  RefreshKind otaPhase(OtaPhase p, const char *why, Ms now);
  // Offerta rifiutata per un motivo che l'utente può risolvere (batteria, cavo, scheda): schermata con il motivo.
  RefreshKind otaRejected(const char *from, const char *to, const char *why, Ms now);
  // Nuova versione confermata dopo il riavvio: toast «Aggiornato alla 0.7.0».
  RefreshKind otaUpdated(const char *version, Ms now);

  // ---------- 0.9.0: Giornata di lavoro e gioco ----------
  // Comando `workday` del PC (già autenticato): come Strumenti › Giornata di lavoro › Inizia ora / Ferma, con gli
  // stessi toast. err = motivo per l'ack ('' = ok): «orario di fine già passato», «ora non ancora nota».
  RefreshKind workdayRemote(bool start, Ms now, const WallClock &wc, char *err, size_t cap);
  // Carte del gioco: sulla scheda esp_random; senza (simulatore, test) xorshift32 su Game::seed.
  void setRandom(RandomFn fn) { rnd_ = fn; }

  // ---------- 0.10.0: gioco della mascotte ----------
  // Messaggio `game` del servizio (applyGame di core/pet): riassunto nuovo e, se c'è, il traguardo (festa o solo toast).
  RefreshKind netGame(const GameMsg &g, Ms now);
  // Un accessorio o le ancore sono arrivati: si ridisegna se il gioco è acceso (la mascotte a schermo potrebbe cambiare).
  RefreshKind netAccessoryArrived(Ms now);
  // Il riassunto del gioco è cambiato (on, livello, indossato, sbloccati…): il loop lo salva in LittleFS. Una volta.
  bool takePetChanged() { const bool c = petChanged_; petChanged_ = false; return c; }

  // ---------- 0.11.0: controller Bluetooth (core/pad, hw/blepad) ----------
  // Evento del controller (pad::MenuMapper), come i gesti del tasto PRG: su/giù = Prev/Next, A = Ok, B = Back,
  // Menu = Menu; HoldDown/HoldUp/HoldFire = il tenuto 3 s, con lo stesso indicatore. Nel Permesso è l'unico modo di
  // rispondere (§7: CONSENTI o NEGA scelto con su/giù, tenuto iniziato su quella richiesta); A premuto lì non risponde.
  // Nel gioco «Alto o basso» su = alta, giù = bassa e A non sceglie.
  RefreshKind padEvent(pad::MenuEvent e, Ms now, const WallClock &wc, pad::Kind src = pad::Kind::Xbox);
  // Stato del collegamento (hw/blepad): toast «Controller collegato», «Controller scollegato», «Associazione scaduta»
  // (questi due solo col Bluetooth acceso di proposito: dopo «Dimentica controller» niente avvisi); ridisegno se cambia
  // qualcosa che si vede. batt −1 = sconosciuta.
  RefreshKind padLinkChanged(pad::Link l, pad::Kind kind, bool xboxBonded, bool phoneBonded, int batt, Ms now);
  RefreshKind padLinkChanged(pad::Link l, bool bonded, int batt, Ms now) {
    return padLinkChanged(l, pad::Kind::Xbox, bonded, m_.sys.padPhoneBonded, batt, now);
  }
  RefreshKind padPasskey(uint32_t key, Ms now);  // 0.14.0: codice da mostrare (mai nel registro)
  RefreshKind padPairFailed(Ms now);             // 0.14.0: telefono non autenticato
  RefreshKind padBondLost(pad::Kind k, Ms now);  // 0.14.0: abbinamento perso (blepad::takeBondLost), riassociare
  // Associa o Dimentica scelti in Menu › Gioco › Controller: il loop li passa a hw/blepad. Si legge una volta.
  PadReq takePadReq() { const PadReq r = padReq_; padReq_ = PadReq::None; return r; }
  // ---------- 0.12.0: giochi Game Boy (core/gb_ui.cpp) ----------
  // Partizione «gbrom» presente e ROM riconosciuta dall'intestazione (title '' = senza titolo): dall'avvio (hw/gb_main).
  void gbInstalled(bool partOk, bool romOk, const char *title, uint32_t romKb);
  // Avvio della partita: schermata GbLoad sopra tutto (Quick). Il loop accende la modalità gioco al giro dopo.
  RefreshKind gbLoad(Ms now);
  // ROM, salvataggio e pannello veloce pronti: GbLoad diventa GbPlay. Da qui il pannello è del driver veloce (None).
  RefreshKind gbPlay(Ms now);
  // Pausa: menu di pausa sopra GbPlay. Quick: il pannello torna all'interfaccia di serie.
  RefreshKind gbPaused(gb::PauseWhy w, Ms now);
  // Ripresa: via il menu di pausa. None: il pannello torna al driver veloce.
  RefreshKind gbResumed(Ms now);
  // Fine (uscita, errore, avvio fallito): via tutte le schermate del gioco; toast msg (nullptr = niente); rest = Riposo.
  RefreshKind gbEnd(const char *msg, bool rest, Ms now);
  // Richiesta dal menu o dai gesti per il loop (una volta).
  gb::GbAct takeGbAct();
  // Partita a schermo col pannello al driver veloce: intanto il loop del piano A non mappa più il controller sui menu
  // (contratto: corpo del piano B).
  bool gbPlaying() const { return top(m_).id == ScreenId::GbPlay; }
  bool gbOnStack() const;  // GbLoad, GbPlay o la pausa nella pila

  Model &model() { return m_; }
  const Model &model() const { return m_; }
  const DeviceProfile &device() const { return dev_; }
  // Fotogramma da disegnare adesso: fr() = frameFor(dev, m, frame, now).
  int frame(Ms now) const { return frameFor(dev_, m_, frame_, now); }
  int rawFrame() const { return frame_; }
  // Testi del pannello «Ultimo comando» (utili in seriale): nome del gesto e risposta di act().
  const char *lastInput() const { return lastInput_; }
  const char *lastAction() const { return lastAction_; }
  // LED che lampeggia finché qualcuno aspetta (solo se il dispositivo ha il LED) e bip del cicalino.
  bool ledOn(Ms now) const;
  bool beeping(Ms now) const { return beepUntil_ && now < beepUntil_; }
  // Voci della lista in cima alla pila (menu o reti). Vuota se la cima non è una lista.
  const MenuList &items(Ms now, const WallClock &wc);
  // 0.13.0: Display › Tema › 2×: k = indice in m.themes, -1 = Calamaio; un indice che non c'è più (catalogo cambiato
  // sotto la lista) = Calamaio. Pubblica per test_theme_menu.
  void themePick(int k, Ms now);

 private:
  friend struct UiProbe;  // test_pad_ui: le seconde barriere di §7, R1 e F11 provate da sole
  void redraw(RefreshKind k) { req_ = mergeRefresh(req_, k); }
  void closeOta();
  void showOta();
  RefreshKind finish(Ms now);
  void normalize(Ms now);
  void setAction(const char *msg);
  void fillItems(const ScreenEntry &tp, Ms now, const WallClock &wc);
  void toggleMute(Ms now);
  bool confirmPerm(ScreenEntry &tp, Ms now);  // false: CONSENTI dal telefono, rifiutato
  // 0.11.0, core/ui_pad.cpp
  RefreshKind padHoldDown(Ms now);
  RefreshKind padHoldUp(Ms now, const WallClock &wc);
  RefreshKind padHoldFire(Ms now, const WallClock &wc);
  RefreshKind padHint(const char *toastMsg, const char *action, Ms now);
  void gbAct(ActId id, Ms now, char *msg, size_t cap);
  // 0.12.0, core/gb_ui.cpp
  bool gbInput(Action a, Ms now);  // gesti sulle schermate del gioco: true = gestito
  void gbActivate(ActId a, Ms now);
  void activate(ScreenEntry &tp, Ms now, const WallClock &wc);
  void doAct(ActId id, Ms now, const WallClock &wc);
  void finishConnect(const ScreenEntry &tp, Ms now, const WallClock &wc);
  void dropSaved(const char *ssid, Ms now);  // via da m.saved; se era la rete in uso, Wi-Fi e PC spariscono
  void changed(Ms now);
  void emitPerm(PermOutcome o, int session, const char *rid, Ms now);
  bool confirmAction(ScreenEntry &tp, Ms now);  // false: dal telefono (R1), resta armata
  void calibTick(Ms t, const WallClock &wc);
  void workTick(Ms t, const WallClock &wc);
  const char *workStartUi(Ms now, const WallClock &wc);  // risposta per «Ultimo comando»; workErr_ = motivo dell'ack
  const char *workStopUi(Ms now);
  void gameAct(bool up, Ms now);
  void petWear(int k, Ms now);              // Accessorio › 2×: k = indice tra gli sbloccati, -1 = nessuno
  void interruptGame(Ms now);  // un permesso in arrivo chiude la partita: Home
  void calibSampleIn(int mv, Ms t, const WallClock &wc);
  void calibAbort(const char *msg, Ms now);
  void pairConfirm(Ms now);
  bool phoneRefused(const char *action, Ms now);  // R1, F11: ingresso del telefono → toast, action, true
  InputSrc src_ = InputSrc::Prg;  // 0.14.0: sorgente dell'ultimo ingresso
  bool fromPhone() const { return src_ == InputSrc::Phone || (m_.ui.holdPad && m_.ui.holdPhone); }
  // R1 e F11, regole 1–3 (DECISIONI «App iPhone»): il telefono non arma, disarma; un tenuto conferma solo se è
  // iniziato dopo l'armatura; un gesto del PRG o dell'Xbox non vale se il telefono ha cambiato qualcosa durante.
  RefreshKind padDispatch(pad::MenuEvent e, Ms now, const WallClock &wc);
  bool phoneDisarm();  // true: c'era qualcosa di armato
  void padInput(const char *what);  // lastInput_: «Telefono: …» o «Controller: …»
  bool phoneRaced() const { return phoneMovedAt_ >= 0 && phoneMovedAt_ + kPhoneSettleMs >= gestureFrom_; }
  Ms keyFrom_ = 0;        // ultimo tasto giù del PRG o dell'Xbox (inizio del tenuto)
  Ms gestureFrom_ = 0;    // prima pressione del gesto in corso del PRG o dell'Xbox
  Ms keyUpAt_ = -1;       // ultimo rilascio del PRG o dell'Xbox (−1 = mai)
  Ms phoneMovedAt_ = -1;  // ultimo evento del telefono che ha cambiato schermata, scelta o voce (−1 = mai)
  bool canAnswer(const Session &s) const;       // il Dettaglio apre la schermata Permesso
  void emitNet(NetCmd c, const char *ssid = "");
  void archive(int i, Ms now);                 // Archivia dal Dettaglio
  void unarchive(int k, Ms now, bool failed);  // archivio k → di nuovo in m.sessions (failed: «Non archiviata»)
  void dropArchived(int k);
  void filterArchived();                       // dopo uno state: toglie le archiviate, lascia tornare le attive
  bool inStack(ScreenId id) const;
  bool showsData() const;  // la schermata in cima mostra dati del PC (serve un ridisegno a ogni state)

  const DeviceProfile &dev_;
  Model m_;
  MenuList &list_;  // sharedMenuList() (0.14.1)
  RefreshKind req_;
  int frame_;
  Ms lastFrame_;
  Ms lastInputAt_;
  int lastMin_;
  Ms lastPage_;
  Ms beepUntil_;
  char lastInput_[32];
  char lastAction_[128];
  PermListener permFn_;
  void *permCtx_;
  PadReq padReq_ = PadReq::None;  // 0.11.0
  bool quietLost_ = false;        // dopo un «Dimentica»: il prossimo scollegamento non si annuncia
  NetListener netFn_;
  void *netCtx_;
  bool provActive_;     // AP e portale accesi su richiesta dell'interfaccia
  uint64_t lastState_;  // impronta dell'ultimo state applicato (senza time): uguale → niente ridisegno
  uint32_t dismissSeq_; // id dei dismiss: «d1», «d2», …
  char permText_[32];   // ultimo «IN ATTESA DA …» disegnato: si ridisegna solo quando cambia
  bool battReal_ = false;  // M6: batteria letta dall'ADC, niente scarica finta
  bool batChanged_ = false;  // M6: calibrazione da salvare o cancellare nella NVS
  RandomFn rnd_ = nullptr;   // 0.9.0: carte del gioco
  char workMsg_[128] = "";   // 0.9.0: risposta di workStartUi/workStopUi
  const char *workErr_ = nullptr;
  bool petChanged_ = false;  // 0.10.0: riassunto del gioco da salvare
  gb::GbAct gbAct_ = gb::GbAct::None;  // 0.12.0: richiesta del gioco in attesa del loop
};

// Righe visibili di Storico e Servizi: il simulatore le calcola nel disegno e ci limita tp.off.
int scrollVisible(const DeviceProfile &dev, ScreenId id);

}  // namespace cal
