// 0.10.2: regola dei completi della scheda (core/refresh, PanelRefresh).
#include <string.h>
#include <unity.h>

#include "core/refresh.h"
#include "core/wake.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int N = 40;  // kHeltec213.fullEvery
const Ms T = 1000000;

void partials(PanelRefresh &c, int n, Ms &now) {
  for (int k = 0; k < n; k++) TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Partial, false, now += 1000));
}

}  // namespace

static void test_quick_is_partial_and_counts_two() {
  PanelRefresh c(N);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Quick, false, T));
  TEST_ASSERT_EQUAL(2, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Partial, false, T));
  TEST_ASSERT_EQUAL(3, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::None, c.apply(RefreshKind::None, false, T));
  TEST_ASSERT_EQUAL(3, c.partials());
}

static void test_threshold_is_deferred() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, N - 1, now);
  TEST_ASSERT_FALSE(c.due());
  // la soglia non fa più il completo subito
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Partial, false, now += 1000));
  TEST_ASSERT_TRUE(c.due());
  partials(c, 10, now);
  TEST_ASSERT_EQUAL(N + 10, c.partials());
}

static void test_deferred_on_rest_entry() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, N, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Quick, true, now));
  TEST_ASSERT_EQUAL(FullWhy::Deferred, c.why());
  TEST_ASSERT_EQUAL(0, c.partials());
  // entrata nel Riposo senza pulizia dovuta: parziale
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Quick, true, now));
  // il disegno che arriva alla soglia entrando nel Riposo fa subito la pulizia
  PanelRefresh d(N);
  partials(d, N - 1, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.apply(RefreshKind::Partial, true, now));
  TEST_ASSERT_EQUAL(FullWhy::Deferred, d.why());
}

static void test_deferred_on_idle() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, N, now);
  TEST_ASSERT_FALSE(c.idleCleanup(now + PanelRefresh::kIdleMs - 1));
  c.input(now + 20000);  // un tasto sposta l'attesa
  TEST_ASSERT_FALSE(c.idleCleanup(now + PanelRefresh::kIdleMs));
  TEST_ASSERT_TRUE(c.idleCleanup(now + 20000 + PanelRefresh::kIdleMs));
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full, false, now + 50000));
  TEST_ASSERT_EQUAL(FullWhy::Deferred, c.why());
  TEST_ASSERT_EQUAL(0, c.partials());
  TEST_ASSERT_FALSE(c.idleCleanup(now + 500000));  // niente di dovuto
  // senza pulizia dovuta l'inattività non fa niente
  PanelRefresh d(N);
  partials(d, N - 1, now);
  TEST_ASSERT_FALSE(d.idleCleanup(now + 10 * PanelRefresh::kIdleMs));
}

static void test_hard_cap() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, 2 * N - 1, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Partial, false, now));
  TEST_ASSERT_EQUAL(FullWhy::Cap, c.why());
  TEST_ASSERT_EQUAL(0, c.partials());
  // un Quick a 2N-1 supera il tetto
  partials(c, 2 * N - 2, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Quick, false, now));
  TEST_ASSERT_EQUAL(FullWhy::Cap, c.why());
}

static void test_before_sleep() {
  Ms now = T;
  PanelRefresh c(N);
  partials(c, 9, now);
  TEST_ASSERT_EQUAL(RefreshKind::None, c.beforeSleep(RefreshKind::None));  // 9: si dorme senza completo
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.beforeSleep(RefreshKind::Partial));  // il disegno in attesa fa 10
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full, false, now));
  TEST_ASSERT_EQUAL(FullWhy::BeforeSleep, c.why());

  PanelRefresh d(N);
  partials(d, 10, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.beforeSleep(RefreshKind::None));
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.apply(RefreshKind::Full, false, now));
  TEST_ASSERT_EQUAL(FullWhy::BeforeSleep, d.why());
  TEST_ASSERT_EQUAL(RefreshKind::None, d.beforeSleep(RefreshKind::None));  // appena fatto: si dorme

  PanelRefresh e(N);
  partials(e, 7, now);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, e.beforeSleep(RefreshKind::Quick));  // 7 + 2 = 9: resta il disegno chiesto
  TEST_ASSERT_EQUAL(RefreshKind::Partial, e.apply(RefreshKind::Quick, false, now));
  TEST_ASSERT_EQUAL(RefreshKind::None, e.beforeSleep(RefreshKind::None));
  TEST_ASSERT_EQUAL(RefreshKind::Full, e.beforeSleep(RefreshKind::Full));
}

static void test_asked_full_unchanged() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, 3, now);
  // Menu › Display › Refresh completo, full-refresh remoto, «PC spento», fine giornata: completo subito
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full, false, now));
  TEST_ASSERT_EQUAL(FullWhy::Asked, c.why());
  TEST_ASSERT_EQUAL(0, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full, true, now));
  TEST_ASSERT_EQUAL(FullWhy::Asked, c.why());
  // un motivo chiesto e poi superato da un parziale non resta appeso
  partials(c, 10, now);
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.beforeSleep(RefreshKind::None));
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Partial, false, now));
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full, false, now));
  TEST_ASSERT_EQUAL(FullWhy::Asked, c.why());
}

static void test_persistence_across_sleep() {
  PanelRefresh c(N);
  Ms now = T;
  partials(c, N + 5, now);  // pulizia dovuta
  static RestKeep k;
  memset(&k, 0, sizeof k);
  k.partials = uint8_t(c.partials());
  restSeal(k);
  TEST_ASSERT_TRUE(restValid(k));
  PanelRefresh d(N);  // dopo il sonno profondo
  d.restore(k.partials, 0);
  TEST_ASSERT_EQUAL(N + 5, d.partials());
  TEST_ASSERT_TRUE(d.due());
  // risveglio dell'ora: già nel Riposo, parziali fino al tetto
  for (int i = N + 5; i < 2 * N - 1; i++) TEST_ASSERT_EQUAL(RefreshKind::Partial, d.apply(RefreshKind::Partial, false, 0));
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.apply(RefreshKind::Partial, false, 0));
  TEST_ASSERT_EQUAL(FullWhy::Cap, d.why());
  // valori fuori misura
  d.restore(250, 0);
  TEST_ASSERT_EQUAL(2 * N - 1, d.partials());
  d.restore(-3, 0);
  TEST_ASSERT_EQUAL(0, d.partials());
}

// Risveglio col tasto (o «resto sveglio» dopo il timer): parziale come un Quick dai parziali salvati.
static void test_key_wake_partial() {
  static RestKeep k;
  memset(&k, 0, sizeof k);
  k.imgOk = 1;
  k.partials = 12;
  restSeal(k);
  TEST_ASSERT_TRUE(wakePartialOk(k, true));
  TEST_ASSERT_FALSE(wakePartialOk(k, false));  // accensione, reset per crash o brownout: completo
  PanelRefresh c(N);
  c.restore(k.partials, T);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Quick, false, T));
  TEST_ASSERT_EQUAL(14, c.partials());  // conta 2
  // pulizia dovuta: rimandata anche al risveglio (Home non è il Riposo)
  PanelRefresh d(N);
  d.restore(N + 3, T);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, d.apply(RefreshKind::Quick, false, T));
  TEST_ASSERT_EQUAL(N + 5, d.partials());
  TEST_ASSERT_TRUE(d.idleCleanup(T + PanelRefresh::kIdleMs));
  // ...ma si fa se il risveglio porta nel Riposo
  d.restore(N + 3, T);
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.apply(RefreshKind::Quick, true, T));
  TEST_ASSERT_EQUAL(FullWhy::Deferred, d.why());
  // tetto: 78 + 2
  d.restore(2 * N - 2, T);
  TEST_ASSERT_EQUAL(RefreshKind::Full, d.apply(RefreshKind::Quick, false, T));
  TEST_ASSERT_EQUAL(FullWhy::Cap, d.why());
}

static void test_key_wake_without_image() {
  static RestKeep k;
  memset(&k, 0, sizeof k);
  k.imgOk = 0;  // immagine mancante
  restSeal(k);
  TEST_ASSERT_FALSE(wakePartialOk(k, true));
  k.imgOk = 1;  // CRC sbagliato (non risigillato)
  TEST_ASSERT_FALSE(wakePartialOk(k, true));
  restSeal(k);
  k.magic = 0;  // memoria RTC mai scritta
  TEST_ASSERT_FALSE(wakePartialOk(k, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_quick_is_partial_and_counts_two);
  RUN_TEST(test_threshold_is_deferred);
  RUN_TEST(test_deferred_on_rest_entry);
  RUN_TEST(test_deferred_on_idle);
  RUN_TEST(test_hard_cap);
  RUN_TEST(test_before_sleep);
  RUN_TEST(test_asked_full_unchanged);
  RUN_TEST(test_persistence_across_sleep);
  RUN_TEST(test_key_wake_partial);
  RUN_TEST(test_key_wake_without_image);
  return UNITY_END();
}
