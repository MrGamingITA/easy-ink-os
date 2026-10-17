// SPIKE usa e getta · blepad: vedi menu_map.h.
#include "menu_map.h"

namespace pad {

namespace {
void push(MenuEvent e, MenuEvent *out, int cap, int *n) {
  if (*n < cap) out[(*n)++] = e;
}
}  // namespace

const char *menuEventName(MenuEvent e) {
  switch (e) {
    case MenuEvent::Up: return "su";
    case MenuEvent::Down: return "giu";
    case MenuEvent::Select: return "scegli";
    case MenuEvent::Back: return "indietro";
    case MenuEvent::HoldStart: return "A tenuto: inizio";
    case MenuEvent::HoldProgress: return "A tenuto: un secondo in piu";
    case MenuEvent::HoldDone: return "A tenuto 3 s: CONFERMA (permesso)";
    case MenuEvent::HoldCancel: return "A rilasciato prima dei 3 s: annullato";
    default: return "-";
  }
}

int MenuMapper::update(const State &s, uint32_t now, MenuEvent *out, int cap) {
  int n = 0;
  const uint8_t dpUp = s.dpad & ~dpad_;  // fronti di salita
  const uint16_t btUp = s.buttons & ~buttons_, btDown = buttons_ & ~s.buttons;
  // Diagonali: conta la componente verticale (su-destra = su)
  if (dpUp & kUp) push(MenuEvent::Up, out, cap, &n);
  if (dpUp & kDown) push(MenuEvent::Down, out, cap, &n);
  if (btUp & kA) {
    // «scegli» alla pressione (menu reattivi); la tenuta parte insieme. Sarà il contesto (schermata
    // permesso) a ignorare «scegli» e ad aspettare HoldDone: una pressione singola non approva mai.
    push(MenuEvent::Select, out, cap, &n);
    holding_ = true;
    holdDone_ = false;
    holdFrom_ = now;
    holdSec_ = 0;
    push(MenuEvent::HoldStart, out, cap, &n);
  }
  if (btUp & kB) push(MenuEvent::Back, out, cap, &n);
  buttons_ = s.buttons;
  dpad_ = s.dpad;
  // Il tempo può essere già oltre i 3 s se il report arriva tardi: prima si guarda il tempo, poi il rilascio.
  n += tick(now, out + n, cap - n);
  if ((btDown & kA) && holding_) {
    if (!holdDone_) push(MenuEvent::HoldCancel, out, cap, &n);
    holding_ = false;
    holdDone_ = false;
    holdSec_ = 0;
  }
  return n;
}

int MenuMapper::tick(uint32_t now, MenuEvent *out, int cap) {
  int n = 0;
  if (!holding_ || holdDone_ || cap <= 0) return 0;
  const uint32_t el = now - holdFrom_;
  if (el >= kHoldMs) {
    holdDone_ = true;
    holdSec_ = int(kHoldMs / 1000);
    push(MenuEvent::HoldDone, out, cap, &n);
    return n;
  }
  const int sec = int(el / 1000);
  if (sec > holdSec_) {
    holdSec_ = sec;
    push(MenuEvent::HoldProgress, out, cap, &n);
  }
  return n;
}

int MenuMapper::reset(MenuEvent *out, int cap) {
  int n = 0;
  if (holding_ && !holdDone_) push(MenuEvent::HoldCancel, out, cap, &n);
  buttons_ = 0;
  dpad_ = 0;
  holding_ = holdDone_ = false;
  holdSec_ = 0;
  return n;
}

}  // namespace pad
