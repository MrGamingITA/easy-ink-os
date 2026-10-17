// Controller Xbox (1914) e telefono (app Calamaio, servizio «Calamaio pad») via Bluetooth LE: NimBLE-Arduino 2.5.1
// come centrale, HID-over-GATT scritto qui (porting di spikes/blepad). Chiavi nella NVS «nimble_bond» (di NimBLE),
// tipo di ogni abbinamento nel libro di core/pad_bonds (calamaio-gb). Un task proprio fa ricerca e collegamento.
//
// Tempi: ogni report porta l'istante d'ARRIVO (millis() nella notifica), letto con polledAt() dopo poll().
// Il MenuMapper va nutrito con quello, mai con l'ora della lettura, e prima vanno svuotati tutti i report,
// poi tick(millis()). Altrimenti una pressione breve arrivata durante un refresh lungo dell'e-ink
// sembrerebbe un tenuto di 3 s e risponderebbe a un permesso. Uso giusto, nel loop:
//   pad::State s;
//   while (blepad::poll(s))  // tutti, con l'ora d'arrivo; il rilascio del mantenimento come reset (mai Select)
//     n = pad::mapPolled(mapper, s, blepad::polledAt(), blepad::polledRelease(), ev, cap);
//   n = mapper.tick(blepad::tickClock(millis()), ev, cap);  // solo dopo; telefono: mai oltre l'ultimo report
#pragma once
#include <stdint.h>

#include "core/pad.h"

namespace cal {
namespace blepad {

// ---- contratto ----
void begin();          // NimBLE e task del controller; si ricollega da solo al primo associato che trova
// 0.14.0: due tipi (spec app iPhone §2.2): un Xbox e un telefono, uno per tipo (libro in calamaio-gb padx/padp)
void startPairing(pad::Kind k = pad::Kind::Xbox);  // 2 minuti per un nuovo di quel tipo (sostituisce solo quel tipo)
void cancelPairing();  // «LUNGA ANNULLA» sul codice del telefono
void forget(pad::Kind k);  // cancella l'abbinamento di quel tipo (a radio spenta: libro subito, NimBLE all'accensione)
void forgetAll();      // Ripristino di fabbrica: tutti, NVS di NimBLE compresa (a radio accesa al più tardi in end())
pad::Link link();
pad::Kind kind();      // tipo del collegamento o dell'associazione in corso
// true con il prossimo stato diverso dall'ultimo (un report alla volta, in ordine); dopo un ricollegamento
// il primo report si consegna sempre. Telefono: 3 s senza report = uno stato vuoto (mantenimento), segnato da
// polledRelease(). I report del telefono uguali all'ultimo (mantenimento ogni 1 s) non escono mai da qui.
bool poll(pad::State &s);
// true se lo stato dell'ultimo poll() riuscito è il «tutto rilasciato» del mantenimento o il report di secondo
// piano del telefono (flags bit0 = 0, revisione finale I2): ai menu va con
// pad::mapPolled (MenuMapper::reset, solo HoldUp, mai Select); al gioco va come uno stato qualsiasi.
bool polledRelease();
bool takePasskey(uint32_t &key);  // codice nuovo da mostrare (una volta); mai nel registro
bool takePairFailed();            // telefono non autenticato: «Associazione non riuscita»
// Un associato di tipo k ha perso l'abbinamento (chiavi tolte, abbinamento rifatto fuori dalla finestra, collegamento
// debole): tolto dal libro, nessuna finestra automatica. Task 11: toast che chiede di riassociarlo. Uno alla volta.
bool takeBondLost(pad::Kind &k);
uint32_t tickClock(uint32_t now); // orologio per MenuMapper::tick (telefono sparito: mai oltre l'ultimo report)

// ---- piano A ----
// Scollega, ferma il task, esegue un forget()/forgetAll() in sospeso, NimBLEDevice::deinit(true): radio spenta, true.
// false se il task non si ferma entro 3 s: niente deinit né cancellazioni col task vivo, radio ancora accesa,
// forget() resta in sospeso; il chiamante riprova più tardi (giro dopo del loop, prima del sonno).
bool end();
bool active();
bool bonded();              // almeno un associato
bool bonded(pad::Kind k);   // associato di quel tipo
int battery();         // 0–100, −1 = sconosciuta
void setScan(bool on); // cercare adesso (pad::radioPlan)
uint32_t polledAt();   // millis() d'ARRIVO dello stato restituito dall'ultimo poll() riuscito (non l'ora della lettura)
// Round 1 del Task 11 (I1): contatore del collegamento (cresce a ogni scollegamento e a begin()). Il loop lo confronta
// con pad::linkEdge: cambiato = collegamento nuovo, MenuMapper azzerato anche da Ready a Ready.
uint8_t connId();

}  // namespace blepad
}  // namespace cal
