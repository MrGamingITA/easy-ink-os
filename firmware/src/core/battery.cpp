#include "core/battery.h"

namespace cal {

namespace {
// OCV di una cella LiPo/Li-ion dal 100 % allo 0 % a passi del 10 %. Fonte: meshtastic/firmware,
// src/power.h, OCV_ARRAY predefinito (usato anche per la variante heltec_wireless_paper).
const int16_t kOcv[11] = {4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100};
}  // namespace

int lipoPercent(int mv) {
  if (mv >= kOcv[0]) return 100;
  if (mv <= kOcv[10]) return 0;
  for (int i = 1; i <= 10; i++) {
    if (mv >= kOcv[i]) {  // fra kOcv[i] (100 − 10 i %) e kOcv[i−1] (10 % in più)
      const int lo = 100 - 10 * i;
      const int span = kOcv[i - 1] - kOcv[i];
      return lo + (10 * (mv - kOcv[i]) + span / 2) / span;
    }
  }
  return 0;
}

int batteryMv(const int *pinMv, const bool *ok, int n) {
  long sum = 0;
  int good = 0;
  for (int i = 0; i < n; i++)
    if (ok[i]) { sum += pinMv[i]; good++; }
  if (good == 0 || good * 2 < n) return -1;
  const int mv = int((sum * kBattDividerNum + good / 2) / good);
  if (mv < kBattMinMv || mv > kBattMaxMv) return -1;
  return mv;
}

}  // namespace cal
