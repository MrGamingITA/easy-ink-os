#include "pad.h"

#include <string.h>

namespace cal {
namespace pad {

namespace {
uint16_t u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

uint16_t hatToButtons(uint8_t hat) {
  static const uint16_t kMap[9] = {0, Up, Up | Right, Right, Down | Right, Down, Down | Left, Left, Up | Left};
  return hat <= 8 ? kMap[hat] : 0;
}

int8_t stickPercent(uint16_t raw) {
  const long c = static_cast<long>(raw) - 32768;
  return static_cast<int8_t>(c * 100 / (c < 0 ? 32768 : 32767));
}

bool parseXboxReport(const uint8_t *r, size_t n, State &out) {
  if (!r || n < kReportLen) return false;
  State s;
  s.lx = stickPercent(u16(r + 0));
  s.ly = stickPercent(u16(r + 2));
  s.rx = stickPercent(u16(r + 4));
  s.ry = stickPercent(u16(r + 6));
  s.lt = u16(r + 8);
  s.rt = u16(r + 10);
  uint16_t b = hatToButtons(r[12]);
  const uint8_t m = r[13], c = r[14], e = r[15];
  if (m & 0x01) b |= A;
  if (m & 0x02) b |= B;
  if (m & 0x08) b |= X;
  if (m & 0x10) b |= Y;
  if (m & 0x40) b |= LB;
  if (m & 0x80) b |= RB;
  if (c & 0x04) b |= View;
  if (c & 0x08) b |= Menu;
  if (c & 0x10) b |= Guide;
  if (c & 0x20) b |= LS;
  if (c & 0x40) b |= RS;
  if (e & 0x01) b |= Share;
  s.buttons = b;
  s.src = Kind::Xbox;
  out = s;
  return true;
}

bool sameState(const State &a, const State &b) {
  return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry && a.lt == b.lt &&
         a.rt == b.rt;
}

const char *kindName(Kind k) { return k == Kind::Phone ? "telefono" : "controller"; }

LinkEdge linkEdge(Link was, uint8_t wasConn, Link now, uint8_t conn) {
  const bool other = conn != wasConn;
  LinkEdge e;
  e.lost = was == Link::Ready && (now != Link::Ready || other);
  e.gained = now == Link::Ready && (was != Link::Ready || other);
  e.reset = e.lost || other;
  return e;
}

const char *linkKey(Link l) {
  switch (l) {
    case Link::Pairing: return "pairing";
    case Link::Connecting: return "connecting";
    case Link::Ready: return "ready";
    default: return "off";
  }
}

const uint32_t MenuMapper::kHoldMs;
const uint32_t MenuMapper::kTapMs;

namespace {
void push(MenuEvent e, MenuEvent *out, int cap, int &n) {
  if (n < cap) out[n++] = e;
}
}  // namespace

const char *menuEventName(MenuEvent e) {
  switch (e) {
    case MenuEvent::Up: return "su";
    case MenuEvent::Down: return "giù";
    case MenuEvent::Select: return "A";
    case MenuEvent::Back: return "B";
    case MenuEvent::Menu: return "menu";
    case MenuEvent::HoldDown: return "A giù";
    case MenuEvent::HoldUp: return "A su";
    case MenuEvent::HoldFire: return "A tenuto 3 s";
    default: return "-";
  }
}

int MenuMapper::update(const State &s, uint32_t now, MenuEvent *out, int cap) {
  int n = 0;
  const uint16_t pressed = s.buttons & ~buttons_, released = buttons_ & ~s.buttons;
  buttons_ = s.buttons;
  n += tick(now, out, cap);  // un report in ritardo può arrivare dopo i 3 s: prima il tempo, poi i tasti
  if (pressed & Up) push(MenuEvent::Up, out, cap, n);
  if (pressed & Down) push(MenuEvent::Down, out, cap, n);
  if (pressed & B) push(MenuEvent::Back, out, cap, n);
  if (pressed & Menu) push(MenuEvent::Menu, out, cap, n);
  if (pressed & A) {
    holding_ = true;
    fired_ = false;
    holdFrom_ = now;
    push(MenuEvent::HoldDown, out, cap, n);
  }
  if ((released & A) && holding_) {
    const bool tap = !fired_ && now - holdFrom_ < kTapMs;
    holding_ = fired_ = false;
    push(MenuEvent::HoldUp, out, cap, n);
    if (tap) push(MenuEvent::Select, out, cap, n);
  }
  return n;
}

int MenuMapper::tick(uint32_t now, MenuEvent *out, int cap) {
  if (!holding_ || fired_ || cap <= 0 || now - holdFrom_ < kHoldMs) return 0;
  fired_ = true;
  out[0] = MenuEvent::HoldFire;
  return 1;
}

int MenuMapper::reset(MenuEvent *out, int cap) {
  int n = 0;
  if (holding_) push(MenuEvent::HoldUp, out, cap, n);
  buttons_ = 0;
  holding_ = fired_ = false;
  return n;
}


const uint32_t LinkMachine::kPairMs;

void LinkMachine::enable(bool on, bool bonded) {
  on_ = on;
  bonded_ = bonded;
  link_ = on && bonded ? Link::Connecting : Link::Off;
}

void LinkMachine::pair(uint32_t now, Kind k) {
  if (!on_) return;
  link_ = Link::Pairing;
  kind_ = k;
  pairUntil_ = now + kPairMs;
}

void LinkMachine::forget() {
  bonded_ = false;
  link_ = Link::Off;
}

void LinkMachine::connected(bool bonded, Kind k) {
  if (!on_) return;
  link_ = Link::Ready;
  bonded_ = bonded;
  kind_ = k;
}

void LinkMachine::cancel() {
  if (link_ == Link::Pairing) link_ = bonded_ ? Link::Connecting : Link::Off;
}

void LinkMachine::lost(uint32_t now, bool bondInvalid) {
  if (!on_) {
    link_ = Link::Off;
    return;
  }
  if (bondInvalid) {
    bonded_ = false;
    pair(now, kind_);
    return;
  }
  if (link_ == Link::Pairing) return;  // si continua a cercare (anche il ricollegamento dopo il report map corto)
  link_ = bonded_ ? Link::Connecting : Link::Off;
}

void LinkMachine::tick(uint32_t now) {
  if (link_ == Link::Pairing && static_cast<int32_t>(now - pairUntil_) >= 0) link_ = bonded_ ? Link::Connecting : Link::Off;
}

RadioOut radioPlan(const RadioIn &in) {
  RadioOut o;
  o.radio = in.setting && !in.asleep && in.wifiPsOk;
  o.scan = false;
  if (!o.radio) return o;
  if (in.link == Link::Pairing) {
    o.scan = true;
    return o;
  }
  if (in.link != Link::Connecting) return o;
  const uint32_t since = in.now - in.searchFrom;
  o.scan = in.usb || !in.idle || since < kSearchMs || since % kIdleScanEveryMs < kIdleScanMs;
  return o;
}

}  // namespace pad
}  // namespace cal
