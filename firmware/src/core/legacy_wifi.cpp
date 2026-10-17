#include "core/legacy_wifi.h"

#include <string.h>

#include "core/crypto.h"

namespace cal {

const char *legacyResultText(LegacyResult r) {
  static const char *const k[] = {"namespace assente", "namespace vuoto", "rete copiata nel vault", "copia non riuscita"};
  return k[static_cast<int>(r)];
}

LegacyResult migrateLegacyWifi(LegacyStore &st, Vault &v) {
  if (!st.openRead()) return LegacyResult::NoNamespace;
  char ssid[Vault::kSsidMax + 1], pass[Vault::kPassMax + 1];
  if (!st.get("ssid", ssid, sizeof ssid) || !ssid[0]) { st.close(); return LegacyResult::Empty; }
  if (!st.get("pass", pass, sizeof pass)) pass[0] = 0;
  st.close();
  char rs[Vault::kSsidMax + 1], rp[Vault::kPassMax + 1];
  const bool ok = v.saveWifi(ssid, pass) && v.loadWifi(rs, sizeof rs, rp, sizeof rp) && !strcmp(ssid, rs) && !strcmp(pass, rp);
  cry::wipe(pass, sizeof pass);
  cry::wipe(rp, sizeof rp);
  if (!ok) return LegacyResult::Failed;
  if (st.openWrite()) {
    st.remove("ssid");
    st.remove("pass");
    st.close();
  }
  return LegacyResult::Migrated;
}

}  // namespace cal
