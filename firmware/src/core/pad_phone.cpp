#include "pad_phone.h"

#include <string.h>

namespace cal {
namespace pad {

const uint32_t Keepalive::kMs;
const uint32_t Keepalive::kFreshMs;
const uint32_t PhoneIdle::kSilentMs;
const uint32_t PhoneIdle::kBackgroundMs;

const char *phoneParseName(PhoneParse p) {
  switch (p) {
    case PhoneParse::BadLen: return "lunghezza";
    case PhoneParse::BadVer: return "versione";
    default: return "ok";
  }
}

namespace {
int8_t clampStick(uint8_t raw) {
  const int v = static_cast<int8_t>(raw);
  return static_cast<int8_t>(v < -100 ? -100 : (v > 100 ? 100 : v));
}
}  // namespace

PhoneParse parsePhoneReport(const uint8_t *r, size_t n, State &out, PhoneMeta &meta) {
  if (!r || n != kPhoneReportLen) return PhoneParse::BadLen;
  if (r[0] != kPhoneReportVer) return PhoneParse::BadVer;
  State s;
  memset(&s, 0, sizeof s);
  s.src = Kind::Phone;
  const bool fg = (r[6] & kPhoneFlagForeground) != 0;
  if (fg) {
    s.buttons = static_cast<uint16_t>(r[2] | (r[3] << 8));
    s.lx = clampStick(r[4]);
    s.ly = clampStick(r[5]);
  }
  meta.seq = r[1];
  meta.fg = fg;
  out = s;
  return PhoneParse::Ok;
}

bool keepaliveRelease(Keepalive &k, const State &last, uint32_t now, State &out, uint32_t &at) {
  if (!k.due(now)) return false;
  const uint32_t when = k.cap(now);
  k.stop();
  const bool any = last.buttons || last.lx || last.ly || last.rx || last.ry || last.lt || last.rt;
  if (!any) return false;
  memset(&out, 0, sizeof out);
  out.src = Kind::Phone;
  at = when;
  return true;
}

}  // namespace pad
}  // namespace cal
