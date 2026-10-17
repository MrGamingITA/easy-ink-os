// Giochi Game Boy: il pezzo di loop() che fa girare le partite (regole di core/gb_session, schermate di cal::Ui,
// emulatore e pannello di hw/gb_run, controller di cal::blepad). Modalità gioco (decisione dell'utente): per tutta la
// partita, pausa compresa, rete spenta (net::suspend: Wi-Fi, WebSocket, mDNS, notifiche e richieste di permesso),
// Bluetooth del controller acceso; all'uscita tutto torna com'era (net::resume, pannello a heltec-eink-modules).
// main.cpp lo chiama in pochi punti: begin in setup, loop prima di powerLoop, ownsPanel prima di present.
#pragma once
#include <stdint.h>

#include "core/ui.h"
#include "gfx/canvas.h"

namespace gbmain {

struct Env {
  bool usb;                   // cavo collegato (UsbMonitor)
  bool battCritical;          // CriticalBattery: senza il gioco la scheda andrebbe in sonno profondo
  cal::WallClock wc;
  Canvas<250, 122> *scratch;  // canvas di main.cpp: durante la partita tiene le bande (present non disegna)
};

void begin(cal::Ui &ui);                                          // setup(): dopo avfs::begin e goLive
cal::RefreshKind loop(cal::Ui &ui, cal::Ms now, const Env &env);  // a ogni giro, prima di powerLoop
// Partita in corso con il pannello al driver veloce: present() non deve toccare il pannello. Un ridisegno chiesto
// intanto rifà solo le bande (bandsDirty).
bool ownsPanel(const cal::Ui &ui);
void bandsDirty();
bool playing();  // partita aperta (avvio, gioco o pausa)

}  // namespace gbmain
