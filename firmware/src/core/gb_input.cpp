#include "gb_input.h"

#include "gb_emu.h"

namespace cal {
namespace gb {

PadKeys mapPad(const pad::State &s) {
  const uint16_t b = s.buttons;
  const bool chord = (b & pad::Menu) && (b & pad::View);
  PadKeys k = {0, chord || (b & pad::Guide) != 0};
  if (k.pause) return k;  // in pausa al gioco non arriva niente
  if (b & pad::A) k.keys |= KeyA;
  if (b & pad::B) k.keys |= KeyB;
  if (b & pad::Menu) k.keys |= KeyStart;
  if (b & pad::View) k.keys |= KeySelect;
  if ((b & pad::Up) || s.ly < -kStickDead) k.keys |= KeyUp;
  if ((b & pad::Down) || s.ly > kStickDead) k.keys |= KeyDown;
  if ((b & pad::Left) || s.lx < -kStickDead) k.keys |= KeyLeft;
  if ((b & pad::Right) || s.lx > kStickDead) k.keys |= KeyRight;
  // Su e giù (o sinistra e destra) insieme non esistono su una croce vera: alcuni giochi si bloccano.
  if ((k.keys & KeyUp) && (k.keys & KeyDown)) k.keys &= uint8_t(~(KeyUp | KeyDown));
  if ((k.keys & KeyLeft) && (k.keys & KeyRight)) k.keys &= uint8_t(~(KeyLeft | KeyRight));
  return k;
}

}  // namespace gb
}  // namespace cal
