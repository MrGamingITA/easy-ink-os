#include "wifi_roam.h"

#include "strbuf.h"

namespace cal {
namespace wifi {

bool seenRssi(const Seen *seen, int nSeen, const char *ssid, int &rssi) {
  bool found = false;
  for (int k = 0; k < nSeen; k++)
    if (streq(seen[k].ssid, ssid) && (!found || seen[k].rssi > rssi)) {
      rssi = seen[k].rssi;
      found = true;
    }
  return found;
}

int roundOrder(const char (*saved)[kSsidLen], int nSaved, const Seen *seen, int nSeen, Candidate *out) {
  int n = 0;
  for (int k = 0; k < nSaved; k++) {
    Candidate c = {k, false, 0};
    c.visible = seenRssi(seen, nSeen, saved[k], c.rssi);
    out[n++] = c;
  }
  // Ordinamento stabile (inserzione): visibili prima, dalla più forte; le altre restano nell'ordine di inserimento.
  for (int a = 1; a < n; a++) {
    const Candidate c = out[a];
    int b = a - 1;
    while (b >= 0 && ((c.visible && !out[b].visible) || (c.visible && out[b].visible && c.rssi > out[b].rssi))) {
      out[b + 1] = out[b];
      b--;
    }
    out[b + 1] = c;
  }
  return n;
}

const char *roamBlockText(const RoamBlock &b) {
  if (b.permOpen) return "richiesta di permesso aperta";
  if (b.ota) return "aggiornamento in corso";
  if (b.calibrating) return "calibrazione della batteria in corso";
  return nullptr;
}

int roamTarget(const char (*saved)[kSsidLen], int nSaved, const char *current, int curRssi, const Seen *seen, int nSeen,
               int *targetRssi) {
  int best = -1, bestRssi = 0;
  for (int k = 0; k < nSaved; k++) {
    if (current && streq(saved[k], current)) continue;
    int r = 0;
    if (!seenRssi(seen, nSeen, saved[k], r)) continue;
    if (r - curRssi < kRoamMarginDb) continue;
    if (best < 0 || r > bestRssi) { best = k; bestRssi = r; }
  }
  if (best >= 0 && targetRssi) *targetRssi = bestRssi;
  return best;
}

}  // namespace wifi
}  // namespace cal
