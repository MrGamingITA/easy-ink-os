#include "refresh.h"

namespace cal {

static int cost(RefreshKind k) { return k == RefreshKind::Quick ? 2 : k == RefreshKind::Partial ? 1 : 0; }

RefreshKind PanelRefresh::apply(RefreshKind req, bool restEntry, Ms now) {
  if (req == RefreshKind::None) return RefreshKind::None;
  last_ = now;
  if (req == RefreshKind::Full) {
    why_ = forced_ != FullWhy::None ? forced_ : FullWhy::Asked;
    forced_ = FullWhy::None;
    partials_ = 0;
    return RefreshKind::Full;
  }
  forced_ = FullWhy::None;
  const int p = partials_ + cost(req);
  if (p >= 2 * fullEvery_ || (restEntry && p >= fullEvery_)) {
    why_ = p >= 2 * fullEvery_ ? FullWhy::Cap : FullWhy::Deferred;
    partials_ = 0;
    return RefreshKind::Full;
  }
  why_ = FullWhy::None;
  partials_ = p;
  return RefreshKind::Partial;
}

bool PanelRefresh::idleCleanup(Ms now) {
  if (!due() || now - last_ < kIdleMs) return false;
  forced_ = FullWhy::Deferred;
  return true;
}

RefreshKind PanelRefresh::beforeSleep(RefreshKind pending) {
  if (pending == RefreshKind::Full) return RefreshKind::Full;
  if (partials_ + cost(pending) >= kSleepFullMin) {
    forced_ = FullWhy::BeforeSleep;
    return RefreshKind::Full;
  }
  return pending;
}

void PanelRefresh::restore(int partials, Ms now) {
  const int cap = 2 * fullEvery_ - 1;
  partials_ = partials < 0 ? 0 : partials > cap ? cap : partials;
  last_ = now;
}

}  // namespace cal
