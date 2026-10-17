#include "menu.h"

#include <string.h>

#include "gb_menu.h"
#include "strbuf.h"
#include "workday.h"

namespace cal {

const char *actKey(ActId a) {
  static const char *const k[] = {"", "hook", "forget", "style", "anim", "speed", "invert", "rot", "full", "mute", "night", "perm",
                                  "sound", "led", "reboot", "sleep", "save", "pairinfo", "unpair", "protect", "factory",
                                  "calib", "newbatt", "forgetnet", "workstart", "workstop", "game", "wear", "gbpair", "gbforget", "gbpick", "gbresume", "gbexit", "theme", "gbpairphone", "gbforgetphone"};
  return k[static_cast<int>(a)];
}

namespace {

// Aggiunge voci a una MenuList tenendo traccia dei troncamenti.
class Builder {
 public:
  explicit Builder(MenuList &out) : o_(out) {
    o_.n = 0;
    o_.truncated = false;
  }
  MenuItem &add(const char *label) {
    MenuItem &it = o_.items[o_.n < kMaxItems ? o_.n++ : kMaxItems - 1];
    memset(&it, 0, sizeof it);
    it.kind = ItemKind::Info;
    it.bars = -1;
    if (!scopy(it.label, label)) o_.truncated = true;
    return it;
  }
  Sb val(MenuItem &it) { return Sb(it.val, sizeof it.val); }
  Sb help(MenuItem &it) { return Sb(it.help, sizeof it.help); }
  void check(const Sb &b) { if (b.truncated()) o_.truncated = true; }
  MenuItem &info(const char *label, const char *val, const char *help) {
    MenuItem &it = add(label);
    if (!scopy(it.val, val ? val : "")) o_.truncated = true;
    if (!scopy(it.help, help)) o_.truncated = true;
    return it;
  }
  MenuItem &go(const char *label, const char *val, MenuId g, const char *help) {
    MenuItem &it = info(label, val, help);
    it.kind = ItemKind::Go;
    it.go = g;
    return it;
  }
  MenuItem &open(const char *label, const char *val, ScreenId s, const char *help) {
    MenuItem &it = info(label, val, help);
    it.kind = ItemKind::Open;
    it.open = s;
    return it;
  }
  MenuItem &act(const char *label, const char *val, ActId a, const char *help) {
    MenuItem &it = info(label, val, help);
    it.kind = ItemKind::Act;
    it.act = a;
    return it;
  }

 private:
  MenuList &o_;
};

void upper(char *dst, size_t cap, const char *src) {
  size_t k = 0;
  for (; src[k] && k + 1 < cap; k++) dst[k] = (src[k] >= 'a' && src[k] <= 'z') ? static_cast<char>(src[k] - 32) : src[k];
  dst[k] = 0;
}

// Stato del controller per Menu › Gioco › Controller.
const char *padStatus(const Model &m, char *v, size_t cap) {
  const Sys &w = m.sys;
  if (!m.set.padOn) return "nessuno associato";  // Bluetooth spento: si accende con «Associa controller»
  switch (w.padLink) {
    case pad::Link::Pairing: return "in associazione";
    case pad::Link::Connecting: return "lo cerco";
    case pad::Link::Ready:
      if (w.padKind == pad::Kind::Phone) return "telefono collegato";
      if (!w.padHasBatt) return "collegato";
      Sb(v, cap).s("collegato · ").i(w.padBatt).s("%");
      return v;
    default: return w.padBonded || w.padPhoneBonded ? "lo cerco" : "nessuno associato";
  }
}

}  // namespace

void menuList(MenuId id, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc, MenuList &out, const char *arg) {
  const bool S = dev.cls == 'S';
  const Sys &w = m.sys;
  Builder B(out);
  char v[kValLen], h[kHelpLen];

  if (id == MenuId::Wifi) {
    out.title = "Wi-Fi";
    if (w.wifi) B.check(Sb(h, sizeof h).s("Il display è collegato a ").s(w.ssid).s("."));
    B.info("Rete", w.wifi ? w.ssid : "nessuna",
           w.wifi ? h : "Nessuna rete salvata: usa Cerca reti o Configura da telefono.");
    v[0] = 0;
    if (w.wifi) Sb(v, sizeof v).i(w.rssi).s(" dBm");
    MenuItem &sig = B.info("Segnale", v, "Sotto −75 dBm gli aggiornamenti possono arrivare in ritardo.");
    sig.hasBars = true;
    sig.bars = w.wifi ? rssiBars(w.rssi) : -1;
    B.info("Indirizzo", w.wifi ? w.ip : "—", "Indirizzo assegnato dal router.");
    Sb(v, sizeof v).i(m.nSaved).s(" di ").i(kSavedMax);
    B.go("Reti salvate", v, MenuId::Saved,
         "Fino a 5 reti. Il display usa la più forte tra quelle che vede e, se il segnale cala sotto −80 dBm, passa da solo a una più forte.");
    B.open("Cerca reti", "", ScreenId::WifiScan, "Elenca le reti vicine con il segnale. Le reti protette si configurano dal telefono.");
    char ap[8];
    upper(ap, sizeof ap, w.boardId);
    B.check(Sb(h, sizeof h).s("Il display crea la rete Calamaio-").s(ap)
                .s(" e mostra un QR: dal telefono scegli la rete e scrivi la password. La rete si aggiunge a quelle salvate."));
    B.open("Configura da telefono", "", ScreenId::Qr, h);
    B.info("Server PC", w.pc ? pcLabel(m) : "non risponde", "Il servizio Calamaio sul PC, trovato in automatico in rete locale.");
    // Brief §8.5: l'URL degli hook diretti è quello della scheda, non calamaio.local (che è il PC).
    B.check(Sb(h, sizeof h).s("Claude Code manda gli eventi direttamente a http://calamaio-").s(w.boardId)
                .s(".local/hook, anche da un server o da un secondo PC. I permessi passano sempre dal servizio sul PC."));
    B.act("Hook diretti", yes(m.set.hookDirect), ActId::Hook, h);
    // Conferma con una seconda doppia pressione entro kForgetMs (come il simulatore).
    B.act("Dimentica tutte le reti", m.ui.forgetUntil > now ? "conferma?" : "", ActId::Forget,
          "Cancella tutte le reti salvate e le password, poi riavvia. Al riavvio parte la configurazione.");
    return;
  }

  // Reti salvate: SSID, «in uso», segnale se la rete si vede (la rete in uso ha il segnale vero, sys.rssi).
  if (id == MenuId::Saved) {
    out.title = "Reti salvate";
    for (int k = 0; k < m.nSaved && k < kSavedMax; k++) {
      const SavedNet &s = m.saved[k];
      const bool cur = w.wifi && streq(w.ssid, s.ssid), seen = cur || s.visible;
      const int r = cur ? w.rssi : s.rssi;
      if (cur) scopy(h, "Il display è collegato a questa rete.");
      else if (seen) B.check(Sb(h, sizeof h).s("Si vede da qui: segnale ").i(r).s(" dBm."));
      else scopy(h, "Non si vede da qui.");
      MenuItem &it = B.go(s.ssid, cur ? "in uso" : "", MenuId::SavedNet, h);
      it.net = k;
      it.hasBars = seen;
      it.bars = seen ? rssiBars(r) : -1;
    }
    if (!out.n) B.info("Nessuna rete", "", "Aggiungi una rete con Configura da telefono o dalla webapp.");
    return;
  }
  if (id == MenuId::SavedNet) {
    const SavedNet *s = nullptr;
    for (int k = 0; k < m.nSaved && !s; k++)
      if (streq(m.saved[k].ssid, arg ? arg : "")) s = &m.saved[k];
    out.title = s ? s->ssid : arg ? arg : "";
    const bool cur = s && w.wifi && streq(w.ssid, s->ssid), seen = cur || (s && s->visible);
    const int r = cur ? w.rssi : s ? s->rssi : 0;
    v[0] = 0;
    if (seen) Sb(v, sizeof v).i(r).s(" dBm");
    else scopy(v, "non visibile");
    MenuItem &sig = B.info("Segnale", v, "Segnale della rete adesso, se si vede.");
    sig.hasBars = true;
    sig.bars = seen ? rssiBars(r) : -1;
    if (cur) scopy(v, "in uso");
    else if (s && s->usedLocal) { const Civil c = civilFrom(s->usedLocal); Sb(v, sizeof v).pad2(c.day).s("/").pad2(c.month); }
    else scopy(v, "mai");
    B.info("Ultimo uso", v, "Ultima volta che il display si è collegato a questa rete.");
    B.act("Dimentica", m.ui.forgetNetUntil > now ? "conferma?" : "", ActId::ForgetNet,
          "Cancella questa rete e la sua password, poi riavvia. Le altre reti salvate restano.");
    return;
  }

  if (id == MenuId::Display) {
    out.title = "Display";
    if (S) B.act("Stile home", kHomeLabel[static_cast<int>(m.set.homeStyle)], ActId::Style, "Goccia, Righe, Contatori o Una alla volta.");
    B.check(Sb(h, sizeof h).s("Auto muove la mascotte a raffiche e quando qualcuno ti aspetta. Su questo pannello le animazioni sono ")
                .s(profileName(dev)).s("."));
    B.act("Animazioni", kAnimLabel[static_cast<int>(m.set.anim)], ActId::Anim, h);
    B.act("Velocità animazioni", kSpeedLabel[static_cast<int>(m.set.speed)], ActId::Speed,
          "Profilo usa la velocità prevista per questo pannello. Massima aggiorna appena il pannello è pronto e usa tutti e 4 i fotogrammi: più fluida, ma consuma di più e lascia più scie.");
    B.act("Risparmio energetico", yes(m.set.save), ActId::Save,
          "Ferma le animazioni e aggiorna lo schermo solo quando cambia qualcosa. Si attiva da solo sotto il 15% di batteria.");
    B.act("Inverti colori", yes(m.set.invert), ActId::Invert, "Testo chiaro su fondo nero.");
    B.act("Ruota 180°", yes(m.set.rot), ActId::Rot, "Per montarlo capovolto, con il cavo dall’altra parte.");
    B.check(Sb(h, sizeof h).s("Pulisce le scie degli aggiornamenti parziali. Parte da solo ogni ").i(dev.fullEvery).s(" aggiornamenti."));
    B.act("Refresh completo", "esegui", ActId::Full, h);
    Sb(v, sizeof v).i(dev.fps).s("/s · ").i(dev.fullEvery);
    B.info("Profilo pannello", v, "Velocità stimata del refresh parziale e ogni quanti aggiornamenti scatta quello completo.");
    B.act("Gioco della mascotte", yes(m.set.game), ActId::Game,
          "Livelli, traguardi e accessori da indossare, guadagnati lavorando con Claude Code. Spento: niente accessori né feste; i progressi restano sul PC.");
    B.go("Tema", m.themes.cur >= 0 && m.themes.cur < m.themes.n ? m.themes.items[m.themes.cur].name : "Calamaio", MenuId::Theme,
         "Tema delle schermate. Calamaio è quello di serie; gli altri arrivano dalla webapp.");
    return;
  }

  // 0.10.0: La mascotte › Accessorio: nessuno o uno degli sbloccati. 2× indossa e lo dice al PC (accessory-wear).
  if (id == MenuId::PetAcc) {
    out.title = "Accessorio";
    const char *wid = m.pet.wear.id;
    MenuItem &none = B.act("Nessuno", wid[0] ? "" : "indossato", ActId::Wear, "La mascotte senza accessori.");
    none.net = -1;
    for (int k = 0; k < m.pet.nUnlocked && k < kPetUnlockedCap; k++) {
      const PetUnlocked &u = m.pet.unlocked[k];
      MenuItem &it = B.act(u.name, streq(u.id, wid) ? "indossato" : "", ActId::Wear,
                           "Sbloccato con il gioco. Lo indossano tutte le mascotte che hanno i punti di aggancio.");
      it.net = k;
    }
    return;
  }

  // 0.13.0: Display › Tema. 2× attiva subito (il loop salva la scelta e la dice al PC).
  if (id == MenuId::Theme) {
    out.title = "Tema";
    MenuItem &c = B.act("Calamaio", m.themes.cur < 0 ? "\xe2\x9c\x93" : "", ActId::Theme, "Il tema di serie: sempre presente.");
    c.net = -1;
    for (int k = 0; k < m.themes.n && k < kThemeCap; k++) {
      MenuItem &it = B.act(m.themes.items[k].name, m.themes.cur == k ? "\xe2\x9c\x93" : "", ActId::Theme,
                           "Tema dalla webapp. Permessi, abbinamento e schermate di sistema restano quelle di serie.");
      it.net = k;
    }
    return;
  }

  if (id == MenuId::Avvisi) {
    out.title = "Avvisi";
    const char *mv = !muted(m, now) ? "no" : (m.set.muteKind == MuteKind::Night ? "fino alle 8" : "1 ora");
    B.act("Silenzia", mv, ActId::Mute, "Ferma i richiami animati e il suono. Le sessioni restano visibili.");
    B.act("Notturno 22-8", yes(m.set.night), ActId::Night, "Di notte niente animazioni e un aggiornamento ogni 5 minuti. Qui lo attivi subito per provarlo.");
    B.act("Permessi dal display", yes(m.set.permOn), ActId::Perm, "Permette di rispondere Consenti o Nega dal display. NEGA è sempre preselezionato.");
    if (dev.buzzer || m.acc)
      B.act(dev.buzzer ? "Cicalino" : "Buzzer esterno", yes(m.set.sound), ActId::Sound, "Un bip quando una sessione chiede un permesso.");
    if (dev.led) B.act("LED", yes(m.set.led), ActId::Led, "Il LED lampeggia finché qualcuno ti aspetta.");
    return;
  }

  if (id == MenuId::Security) {
    out.title = "Sicurezza";
    pairLabel(m, v, sizeof v);
    B.act("Abbinamento", v, ActId::PairInfo,
          "Solo il PC abbinato può far rispondere ai permessi da qui. Per abbinare: sul PC esegui calamaio pair, oppure Abbina nella pagina Dispositivo.");
    B.act("Dimentica abbinamento", m.ui.unpairUntil > now ? "conferma?" : "", ActId::Unpair,
          "Cancella la chiave dell'abbinamento. I permessi tornano a rispondersi solo nel terminale.");
    B.act("Proteggi le chiavi", m.sys.keysProtected ? "protette" : "non protette", ActId::Protect,
          "Scrive una chiave segreta nel chip, una volta sola e per sempre, e cifra con quella la password del Wi-Fi e l'abbinamento.");
    B.act("Ripristino di fabbrica", "", ActId::Factory,
          "Cancella Wi-Fi, abbinamento, impostazioni e sale delle chiavi, poi riavvia. La chiave nel chip resta.");
    return;
  }

  if (id == MenuId::Battery) {  // M6: livello (la vista di prima), calibrazione, nuova batteria
    out.title = "Batteria";
    Sb(v, sizeof v).i(jsRound(w.batt)).s("%");
    B.open("Livello", v, ScreenId::Batteria, "Livello, ricarica, autonomia stimata e modalità risparmio.");
    if (m.bat.cal.valid) {
      const Civil c = civilFrom(m.bat.cal.atLocal);
      Sb(v, sizeof v).pad2(c.day).s("/").pad2(c.month);
    } else {
      scopy(v, "mai");
    }
    B.act("Calibra la batteria", v, ActId::Calib,
          "Due ore a batteria, partendo almeno dal 90% e senza cavo: misura i consumi veri. Da rifare ogni volta che cambi la batteria. Stima ±30%.");
    B.act("Nuova batteria", m.ui.newBattUntil > now ? "conferma?" : "", ActId::NewBatt,
          "Cancella la calibrazione. Da rifare ogni volta che cambi la batteria.");
    return;
  }

  if (id == MenuId::Info) {
    out.title = "Info";
    Sb(v, sizeof v).s("Calamaio OS ").s(uiVersion(m));
    B.info("Sistema", v, "Versione del firmware.");
    B.info("Accessori", m.acc ? "CO2 e buzzer" : "nessuno", "Sensore e buzzer collegati ai pin di espansione.");
    {
      // dev.size.replace('″', '"')
      char sz[16];
      Sb z(sz, sizeof z);
      for (const char *p = dev.size; *p;) {
        if (startsWith(p, "″")) { z.ch('"'); p += strlen("″"); }
        else { z.ch(*p); p++; }
      }
      B.check(Sb(h, sizeof h).s(dev.name).s(" ").s(sz).s("."));
      B.info("Dispositivo", dev.name, h);
    }
    Sb(v, sizeof v).i(dev.W).s("×").i(dev.H).s(dev.gray ? " · 16 grigi" : " · b/n");
    B.info("Schermo", v, "Risoluzione e colori del pannello.");
    Sb(v, sizeof v).i(dev.fps).s(" agg./s · ").i(dev.fullEvery);
    B.info("Refresh", v, "Velocità stimata e ogni quanti aggiornamenti parte il refresh completo.");
    Sb(v, sizeof v).s(dev.input == InputKind::Single ? "1 tasto" : dev.input == InputKind::Wheel ? "rotella + 2 tasti" : "3 tasti")
        .s(dev.touch ? " + tocco" : "");
    B.info("Comandi", v, "Tasti disponibili su questo dispositivo.");
    B.info("Indirizzo", w.wifi ? w.ip : "—", "Indirizzo in rete locale.");
    ago(now, w.bootAt, v, sizeof v);
    B.info("Acceso da", v, "Tempo dall’ultimo avvio.");
    // M6: diagnosi del rilevamento del cavo (a batteria non c'è la seriale): campioni alti su quelli letti.
    if (!w.live) scopy(v, w.usb ? "sì (8/8)" : "no (0/8)");
    else if (!w.usbN) scopy(v, "?");
    else Sb(v, sizeof v).s(w.usbHigh > 0 ? "sì (" : "no (").i(w.usbHigh).s("/").i(w.usbN).s(")");
    B.info("Cavo USB", v, "Letto dalla linea del convertitore USB-seriale ogni 10 secondi: campioni alti su 8. Il fulmine vuol dire cavo collegato, non carica in corso.");
    B.act("Riavvia", "", ActId::Reboot, "Riavvia il sistema e rifà la connessione.");
    return;
  }

  if (id == MenuId::Tools) {
    out.title = "Strumenti";
    int down = 0;
    for (int k = 0; k < m.nServices; k++)
      if (!m.services[k].ok) down++;
    const AgendaItem *nx = 0;
    for (int k = 0; k < m.nAgenda && !nx; k++)
      if (m.agenda[k].t > now) nx = &m.agenda[k];
    char hh[8];
    if (focusOn(m, now)) { hmAt(wc, now, m.focus.until, hh, sizeof hh); Sb(v, sizeof v).s("fino alle ").s(hh); }
    else Sb(v, sizeof v).i(m.focus.len).s(" min");
    B.open("Concentrazione", v, ScreenId::Focus, "Timer da 25 minuti: Goccia lavora con te e gli avvisi restano in silenzio finché non finisce.");
    if (nx) hmAt(wc, now, nx->t, v, sizeof v);
    else scopy(v, "—");
    B.open("Agenda", v, ScreenId::Agenda, "Prossimi appuntamenti da un calendario ICS, letti dal servizio sul PC.");
    if (down) Sb(v, sizeof v).i(down).s(" non risponde");
    else scopy(v, "tutto ok");
    B.open("Servizi e PR", v, ScreenId::Servizi, "Siti e servizi controllati dal PC, più lo stato delle pull request letto con gh.");
    B.open("Wi-Fi ospiti", "", ScreenId::Guest, "QR della rete ospiti: chi lo inquadra si collega senza scrivere la password.");
    if (m.acc) Sb(v, sizeof v).i(jsRound(m.air.co2)).s(" ppm");
    else if (dev.sensor) { char t[16]; fmt1(m.sys.temp, t, sizeof t); Sb(v, sizeof v).s(t).s("°"); }
    else scopy(v, "nessun sensore");
    B.open("Ambiente", v, ScreenId::Ambiente, "CO2, temperatura e umidità da un sensore sui pin I2C.");
    if (m.work.on) { hmLocal(m.work.endLocal, hh, sizeof hh); Sb(v, sizeof v).s("fino alle ").s(hh); }
    else scopy(v, "spenta");
    B.go("Giornata di lavoro", v, MenuId::Work, "Quanto manca alla fine della giornata, nel Riposo e accanto all’orologio della Home.");
    return;
  }

  // 0.9.0: la fine è fissa all'orario di fine (regola A). Inizio e Fine si cambiano dalla webapp.
  if (id == MenuId::Work) {
    out.title = "Giornata di lavoro";
    char a[8], b[8];
    minHm(m.set.workStart, a, sizeof a);
    minHm(m.set.workEnd, b, sizeof b);
    B.check(Sb(h, sizeof h).s("Conto alla rovescia fino alle ").s(b).s(": se inizi più tardi la giornata è più corta."));
    B.act("Inizia ora", m.work.on ? "attiva" : "", ActId::WorkStart, h);
    B.act("Ferma", "", ActId::WorkStop, "Spegne il conto alla rovescia. Si spegne da solo alla fine e a mezzanotte.");
    B.info("Inizio", a, "Inizio della giornata: la barra del Riposo parte da qui. Si cambia dalla webapp.");
    B.info("Fine", b, "Fine della giornata. Si cambia dalla webapp.");
    return;
  }

  if (id == MenuId::Gb) {  // 0.11.0: Menu › Gioco (nel simulatore solo «Alto o basso»)
    out.title = "Gioco";
    if (w.padHw) {  // solo sulla scheda vera
      B.go("Controller", padStatus(m, v, sizeof v), MenuId::GbPad,
           "Associa il controller Xbox o il telefono, guardane lo stato o dimenticali. Uno alla volta.");
      B.go("ROM", "", MenuId::GbRoms, "I giochi del Game Boy caricati sul PC (pagina Giochi della webapp) e quello già sulla scheda.");
    }
    Sb(v, sizeof v).s("record ").i(m.game.best);
    B.open("Alto o basso", v, ScreenId::Game, "Indovina se la prossima carta sarà più alta o più bassa: 1× più alta, 2× più bassa.");
    return;
  }
  if (id == MenuId::GbPad) {  // 0.11.0: Menu › Gioco › Controller
    out.title = "Controller";
    const bool pairing = w.padLink == pad::Link::Pairing;
    B.info("Stato", padStatus(m, v, sizeof v),
           "Croce su e giù: sposta. A: scegli. B: indietro. Tasto Menu: apre il Menu. Ai permessi rispondi tenendo premuto A per 3 secondi. Dal telefono non si confermano CONSENTI, Proteggi, Ripristino, abbinamento col PC e Wi-Fi.");
    B.act("Associa controller", pairing && w.padKind == pad::Kind::Xbox ? "in corso" : "", ActId::GbPair,
          "Tieni premuto il tasto di associazione del controller finché il logo Xbox lampeggia veloce. Il nuovo sostituisce il vecchio controller, mai il telefono.");
    B.act("Dimentica controller", m.ui.gbForgetUntil > now ? "conferma?" : "", ActId::GbForget,
          "Cancella l'associazione del controller. Il Bluetooth si spegne se non resta associato il telefono.");
    B.act("Associa telefono", pairing && w.padKind == pad::Kind::Phone ? "in corso" : "", ActId::GbPairPhone,
          "Apri l'app Calamaio sull'iPhone, scheda Controller: la scheda la trova e mostra un codice da scrivere sul telefono. Il nuovo sostituisce il vecchio telefono, mai il controller.");
    B.act("Dimentica telefono", m.ui.gbForgetPhoneUntil > now ? "conferma?" : "", ActId::GbForgetPhone,
          "Cancella l'associazione del telefono. Il Bluetooth si spegne se non resta associato il controller.");
    return;
  }
  if (id == MenuId::GbPause) {  // 0.12.0: pausa della partita (core/gb_ui)
    out.title = "Pausa";
    if (m.gb.pause == gb::PauseWhy::Controller && w.padKind == pad::Kind::Phone)  // F7 (0.14.0)
      B.info("Telefono scollegato", "", "Riapri l'app Calamaio sulla scheda Controller: si ricollega da solo. Poi Riprendi.");
    else if (m.gb.pause == gb::PauseWhy::Controller)
      B.info("Controller scollegato", "", "Riaccendi il controller con il tasto Xbox: si ricollega da solo. Poi Riprendi.");
    else if (m.gb.pause == gb::PauseWhy::Idle)
      B.info("Pausa automatica", "", "5 minuti senza tasti. Dopo 10 minuti in pausa la partita si salva e la scheda va a riposo.");
    B.act("Riprendi", "", ActId::GbResume, "Torna alla partita.");
    B.act("Salva ed esci", "", ActId::GbExit, "Salva la partita sulla scheda e torna al menu. Wi-Fi e avvisi si riaccendono.");
    return;
  }
  if (id == MenuId::GbRoms) {  // 0.11.0: Menu › Gioco › ROM; le voci le scrive il piano B (core/gb_menu)
    out.title = "ROM";
    const int k = gbGameItems(m, now, out.items, kMaxItems);
    out.n = k < 0 ? 0 : (k > kMaxItems ? kMaxItems : k);
    if (out.n == 0)
      B.info("Nessuna ROM", "", "Le ROM si caricano sul PC dalla webapp (pagina Giochi) e arrivano qui col PC collegato.");
    return;
  }

  out.title = "Menu";
  if (m.limits.hasH5) Sb(v, sizeof v).s("5h ").i(jsRound(m.limits.h5)).s("%");
  else scopy(v, "5h —");
  B.open("Limiti", v, ScreenId::Limiti, "Uso delle finestre da 5 ore e da 7 giorni (piani Pro e Max) e contesto di ogni sessione, dalla status line di Claude Code.");
  B.go("Strumenti", "", MenuId::Tools, "Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.");
  if (m.set.game) {  // 0.10.0: solo con il gioco della mascotte acceso
    v[0] = 0;
    if (m.pet.known) Sb(v, sizeof v).s("livello ").i(m.pet.level);
    B.open("La mascotte", v, ScreenId::Pet, "Livello, energia, ultimi traguardi e accessorio della mascotte.");
  }
  B.go("Gioco", w.padHw && m.set.padOn && w.padLink == pad::Link::Ready ? "collegato" : "", MenuId::Gb,
       "Alto o basso e, col controller Bluetooth, i giochi del Game Boy.");  // 0.11.0: sempre, anche nel simulatore
  B.go("Wi-Fi", w.wifi ? w.ssid : "non connesso", MenuId::Wifi, "Rete, segnale, configurazione dal telefono e collegamento al PC.");
  B.go("Display", "", MenuId::Display, "Stile della home, animazioni, colori invertiti, rotazione e refresh.");
  B.go("Avvisi", muted(m, now) ? "silenziati" : (m.set.night ? "notturno" : ""), MenuId::Avvisi,
       dev.buzzer ? "Silenzia, notturno, risposte ai permessi, cicalino e LED." : "Silenzia, notturno, risposte ai permessi.");
  B.go("Sicurezza", pairShort(m), MenuId::Security, "Abbinamento con il PC, chiavi nel chip e ripristino di fabbrica.");
  B.open("Oggi", "", ScreenId::Oggi, "Task completati, permessi e tempo medio di attesa della giornata.");
  B.open("Storico", "", ScreenId::Storico, dev.sd ? "Tutti gli eventi, salvati sulla scheda SD, un file al giorno." : "Gli ultimi 80 eventi, tenuti in memoria.");
  Sb(v, sizeof v).i(jsRound(w.batt)).s("%");
  B.go("Batteria", v, MenuId::Battery, "Livello, ricarica, autonomia e calibrazione.");
  if (S) B.open("Stato", "", ScreenId::Stato, "Wi-Fi, batteria e PC in grande.");
  B.act("Riposo", "", ActId::Sleep, "Orologio grande e mascotte che dorme; un tasto la sveglia.");
  B.go("Info", "", MenuId::Info, "Versione, schermo, profilo di refresh e indirizzo.");
}

void netItems(const Model &m, MenuList &out) {
  Builder B(out);
  out.title = "Reti vicine";
  char lab[kLabelLen], h[kHelpLen];
  for (int k = 0; k < m.nNets && k < kNetCap; k++) {
    const NetEntry &n = m.nets[k];
    Sb l(lab, sizeof lab);
    l.s(n.ssid);
    if (m.sys.wifi && streq(m.sys.ssid, n.ssid)) l.s("  ✓");
    B.check(Sb(h, sizeof h).s(n.lock ? "Rete protetta: la password si inserisce dal telefono." : "Rete aperta: si collega subito.")
                .s(" Segnale ").i(n.rssi).s(" dBm."));
    MenuItem &it = B.info(lab, "", h);
    it.kind = ItemKind::Net;
    it.net = k;
    it.hasBars = true;
    it.bars = rssiBars(n.rssi);
    it.lock = n.lock;
  }
}

// POD: zero in .bss, nessuna guardia d'inizializzazione.
MenuList &sharedMenuList() {
  static MenuList list;
  return list;
}

}  // namespace cal
