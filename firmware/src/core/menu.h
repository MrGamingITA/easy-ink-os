// Menu e liste: porting di menuList() e netItems() del simulatore.
// Le voci si ricostruiscono a ogni uso dal modello, come fa il simulatore a ogni disegno.
#pragma once
#include <stdint.h>

#include "model.h"

namespace cal {

enum class ItemKind : uint8_t {
  Info,  // solo lettura
  Go,    // it.go: apre un sottomenu (lista)
  Open,  // it.open: apre una schermata
  Act,   // it.act: esegue un'azione (doAct)
  Net,   // voce di netItems: una rete di NETS
};

enum class ActId : uint8_t {
  None, Hook, Forget, Style, Anim, Speed, Invert, Rot, Full, Mute, Night, Perm, Sound, Led, Reboot, Sleep,
  Save, PairInfo, Unpair, Protect, Factory,
  Calib, NewBatt,  // M6: Menu › Batteria
  ForgetNet,       // 0.8.0: Reti salvate › una rete › Dimentica
  WorkStart, WorkStop,  // 0.9.0: Strumenti › Giornata di lavoro › Inizia ora, Ferma
  Game,                 // 0.10.0: Display › Gioco della mascotte
  Wear,                 // 0.10.0: La mascotte › Accessorio › una voce (net = indice tra gli sbloccati, -1 = nessuno)
  GbPair, GbForget,     // 0.11.0: Menu › Gioco › Controller
  GbPick, GbResume, GbExit,  // 0.12.0: Menu › Gioco › ROM › il gioco; pausa: Riprendi, Salva ed esci
  Theme,                // 0.13.0: Display › Tema › un tema (net = indice, -1 = Calamaio)
  GbPairPhone, GbForgetPhone,  // 0.14.0: Menu › Gioco › Controller, telefono
};
const char *actKey(ActId a);  // 'hook', 'forget', … come it.act

const int kMaxItems = 22;  // 0.10.0: Menu con «La mascotte» (13), Accessorio (nessuno + 20 sbloccati)
const int kLabelLen = 40;
const int kValLen = 48;
const int kHelpLen = 224;

struct MenuItem {
  char label[kLabelLen];
  char val[kValLen];   // '' = nessun valore (drawVal non lo scrive)
  char help[kHelpLen];
  ItemKind kind;
  MenuId go;           // kind == Go
  ScreenId open;       // kind == Open
  ActId act;           // kind == Act
  int net;             // kind == Net: indice in m.nets; go SavedNet: indice in m.saved; act Wear: sbloccato (-1 nessuno); act Theme: tema (-1 Calamaio)
  bool hasBars;        // it.bars !== undefined
  int bars;            // −1 = nessuna rete (icona barrata)
  bool lock;
  // drawVal() disegna «›» per go e open.
  bool chevron() const { return kind == ItemKind::Go || kind == ItemKind::Open; }
};

struct MenuList {
  const char *title;
  MenuItem items[kMaxItems];
  int n;
  bool truncated;  // vero se un testo non è entrato nel suo buffer (i test lo vietano)
};

// 0.14.1: una sola MenuList (7 KB) per cal::Ui e ui::draw. Chi la legge la riempie nello stesso giro (menuList,
// netItems) e non tiene riferimenti oltre la chiamata; solo il loop la usa.
MenuList &sharedMenuList();

// arg: SSID della rete per MenuId::SavedNet (tp.ssid della lista aperta), come menuList(id, dev, m, arg) del simulatore.
void menuList(MenuId id, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc, MenuList &out, const char *arg = "");
void netItems(const Model &m, MenuList &out);

}  // namespace cal
