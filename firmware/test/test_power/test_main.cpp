// M6: energia (core/power), cavo USB con il CP2102 «fantasma» (core/usb), calibrazione della batteria e
// autonomia (core/ui, core/model), bye con 60 s di grazia, riprova ogni 15 s (core/retry).
#include <math.h>
#include <string.h>
#include <unity.h>

#include "core/menu.h"
#include "core/power.h"
#include "core/retry.h"
#include "core/ui.h"
#include "core/usb.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const Ms kT0 = 1791707400000LL;  // 2026-10-11 10:30 (Roma), come test_ui
WallClock wcAt(Ms now) { WallClock w; w.localMs = now + 2 * 3600000LL; return w; }

PowerInputs idleIn() {
  PowerInputs in;
  memset(&in, 0, sizeof in);
  in.pc = true;
  return in;
}

// ---------------------------------------------------------------- stati di alimentazione

void test_active_rules_and_idle_after_60s() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  p.update(in, 1000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));  // tasto (avvio) da meno di 60 s
  TEST_ASSERT_TRUE(p.update(in, 60000));
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
  TEST_ASSERT_EQUAL_STRING("sessioni ferme e nessun tasto da 60 s", p.why());
  in.busy = true;
  TEST_ASSERT_TRUE(p.update(in, 61000));
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  in.busy = false;
  in.permOpen = true;
  p.update(in, 62000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  in.permOpen = false;
  p.update(in, 63000);
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
  p.key(64000);
  p.update(in, 64000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  TEST_ASSERT_FALSE(p.update(in, 123999));
  TEST_ASSERT_TRUE(p.update(in, 124000));
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
}

void test_usb_always_active_never_sleeps() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  in.usb = true;
  p.update(in, 10 * 60000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  TEST_ASSERT_EQUAL_STRING("cavo USB collegato", p.why());
  p.rest(10 * 60000);  // Riposo col cavo: solo la schermata
  p.update(in, 10 * 60000 + 1);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  p.bye(11 * 60000);
  in.pc = false;
  in.pcOff = true;
  p.update(in, 30 * 60000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  in.battCritical = true;  // con il cavo la batteria non è un problema
  p.update(in, 31 * 60000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
}

void test_rest_without_usb_sleeps() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  in.busy = true;
  p.rest(5000);
  TEST_ASSERT_TRUE(p.update(in, 5000));
  TEST_ASSERT_EQUAL(int(PowerState::Sleep), int(p.state()));
  TEST_ASSERT_EQUAL(int(SleepReason::Rest), int(p.reason()));
  p.cancelRest();  // un tasto ha tolto il Riposo prima del sonno
  p.update(in, 5001);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
}

// Bye: riepilogo subito, poi 60 s a cercare il PC; se torna (riavvio del servizio) niente sonno.
void test_bye_grace_then_sleep() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  p.update(in, 120000);
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
  p.bye(200000);
  in.pc = false;
  in.pcOff = true;
  p.update(in, 200000);
  TEST_ASSERT_NOT_EQUAL(int(PowerState::Sleep), int(p.state()));
  p.update(in, 259999);
  TEST_ASSERT_NOT_EQUAL(int(PowerState::Sleep), int(p.state()));
  TEST_ASSERT_TRUE(p.update(in, 260000));
  TEST_ASSERT_EQUAL(int(PowerState::Sleep), int(p.state()));
  TEST_ASSERT_EQUAL(int(SleepReason::Bye), int(p.reason()));
  TEST_ASSERT_EQUAL_STRING("PC spento: nessuna risposta per 60 s dopo il bye", p.why());
}

void test_bye_then_service_restarts_no_sleep() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  p.update(in, 120000);
  p.bye(200000);
  in.pc = false;
  in.pcOff = true;
  p.update(in, 210000);
  in.pc = true;     // welcome/state del servizio riavviato
  in.pcOff = false;
  p.update(in, 215000);
  in.pc = false;    // cade di nuovo, ma senza bye: «PC non raggiungibile», niente sonno
  p.update(in, 400000);
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
}

void test_bye_respects_recent_key() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  p.bye(100000);
  in.pc = false;
  in.pcOff = true;
  p.key(150000);
  p.update(in, 170000);  // 70 s dal bye ma solo 20 s dal tasto
  TEST_ASSERT_NOT_EQUAL(int(PowerState::Sleep), int(p.state()));
  p.update(in, 210000);
  TEST_ASSERT_EQUAL(int(PowerState::Sleep), int(p.state()));
}

void test_button_wake_after_bye_sleeps_again_after_60s() {
  PowerManager p;
  p.begin(0);  // risveglio dal tasto: begin() = tasto adesso
  PowerInputs in = idleIn();
  in.pc = false;
  in.pcOff = true;  // riepilogo dall'istantanea, nessun bye in questo avvio
  p.update(in, 59999);
  TEST_ASSERT_NOT_EQUAL(int(PowerState::Sleep), int(p.state()));
  p.update(in, 60000);
  TEST_ASSERT_EQUAL(int(PowerState::Sleep), int(p.state()));
  TEST_ASSERT_EQUAL_STRING("PC spento e nessun tasto da 60 s", p.why());
}

void test_calibration_blocks_sleep_and_sets_state() {
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  in.calibrating = true;
  p.rest(1000);
  p.update(in, 120000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(p.state()));
  in.calibIdle = true;
  in.busy = true;  // fase 2: Attesa anche con una sessione al lavoro
  p.update(in, 130000);
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
  in.calibrating = in.calibIdle = false;
  in.busy = false;
  p.update(in, 140000);  // il Riposo chiesto durante la calibrazione non resta in sospeso
  TEST_ASSERT_EQUAL(int(PowerState::Idle), int(p.state()));
}

void test_battery_critical() {
  CriticalBattery c;
  TEST_ASSERT_FALSE(c.update(3250, false));
  TEST_ASSERT_FALSE(c.update(-1, false));  // lettura scartata: non conta
  TEST_ASSERT_TRUE(c.update(3290, false));
  TEST_ASSERT_FALSE(c.update(3290, true));  // cavo: si azzera
  TEST_ASSERT_FALSE(c.update(3250, false));
  TEST_ASSERT_FALSE(c.update(3400, false));  // una buona azzera
  TEST_ASSERT_FALSE(c.update(3250, false));
  PowerManager p;
  p.begin(0);
  PowerInputs in = idleIn();
  in.busy = true;
  in.battCritical = true;
  p.update(in, 1000);
  TEST_ASSERT_EQUAL(int(PowerState::Sleep), int(p.state()));
  TEST_ASSERT_EQUAL(int(SleepReason::Battery), int(p.reason()));
}

void test_power_inputs_from_model() {
  static Model m;
  newModel(m, kT0, wcAt(kT0));
  resetStack(m, makeScreen(ScreenId::Home));
  PowerInputs in = powerInputs(m);
  TEST_ASSERT_TRUE(in.busy);  // sessioni finte: una lavora, una chiede il permesso
  m.sys.pc = false;           // dopo un bye l'ultimo state non conta
  in = powerInputs(m);
  TEST_ASSERT_FALSE(in.busy);
  TEST_ASSERT_FALSE(in.permOpen);
  TEST_ASSERT_FALSE(in.playing);
  pushScreen(m, makeScreen(ScreenId::PadCode));  // F6 (0.14.0): il codice del telefono tiene sveglia la scheda
  TEST_ASSERT_TRUE(powerInputs(m).playing);
}

// ---------------------------------------------------------------- risveglio da timer

void test_timer_wake_decisions() {
  TimerWakeOutcome o = timerWakeDecision(SleepReason::Bye, false, false, false, false);
  TEST_ASSERT_FALSE(o.stayAwake);
  TEST_ASSERT_FALSE(o.redraw);  // il riepilogo «PC spento alle …» non cambia
  o = timerWakeDecision(SleepReason::Bye, true, false, false, false);
  TEST_ASSERT_TRUE(o.stayAwake);
  o = timerWakeDecision(SleepReason::Rest, false, false, false, false);
  TEST_ASSERT_FALSE(o.stayAwake);
  TEST_ASSERT_TRUE(o.redraw);  // l'ora del Riposo
  o = timerWakeDecision(SleepReason::Rest, true, false, false, false);
  TEST_ASSERT_FALSE(o.stayAwake);
  TEST_ASSERT_TRUE(o.redraw);
  o = timerWakeDecision(SleepReason::Rest, true, true, false, false);
  TEST_ASSERT_TRUE(o.stayAwake);
  TEST_ASSERT_TRUE(timerWakeDecision(SleepReason::Rest, false, false, true, false).stayAwake);
  TEST_ASSERT_TRUE(timerWakeDecision(SleepReason::Bye, false, false, false, true).stayAwake);
}

// Dopo un bye: subito, 5 s, 5 s, poi ogni 15 s fino ai 60 s (0, 5, 10, 25, 40, 55).
void test_retry_after_bye_every_15s() {
  RetrySchedule r;
  r.restart(0, kByeRetryMs);
  const uint32_t want[] = {0, 5000, 10000, 25000, 40000, 55000, 70000};
  for (uint32_t w : want) {
    TEST_ASSERT_TRUE(r.due(w));
    if (w) TEST_ASSERT_FALSE(r.due(w - 1));
    r.attempt(w);
  }
  r.setSlow(kRetrySlowMs);  // fine della grazia (col cavo): di nuovo ogni 30 s (dal tentativo dopo)
  TEST_ASSERT_TRUE(r.due(85000));
  r.attempt(85000);
  TEST_ASSERT_EQUAL_UINT32(30000, r.nextDelay());
  RetrySchedule d;
  d.restart(0);
  TEST_ASSERT_EQUAL_UINT32(30000, d.delay(3));  // senza bye come prima
}

// ---------------------------------------------------------------- batteria: pendenza, registro, notti, NVS

void test_trend_needs_two_hours_and_resets_with_usb() {
  BatteryTrend t;
  double r = 0;
  for (int k = 0; k <= 12; k++) t.add(Ms(k) * 5 * 60000, mvFromPercent(80 - 0.5 * k), false);  // 6 %/h per 1 h
  TEST_ASSERT_FALSE(t.rate(r));
  for (int k = 13; k <= 30; k++) t.add(Ms(k) * 5 * 60000, mvFromPercent(80 - 0.5 * k), false);
  TEST_ASSERT_TRUE(t.rate(r));
  TEST_ASSERT_FLOAT_WITHIN(0.15, 6.0, r);
  t.add(Ms(31) * 5 * 60000, 4100, true);
  TEST_ASSERT_EQUAL(0, t.count());
  t.add(0, 3900, false);
  t.add(60000, 3890, false);  // meno di 5 minuti: non entra
  TEST_ASSERT_EQUAL(1, t.count());
}

void test_ledger_mix_and_hours() {
  EnergyLedger l;
  l.clear();
  const int64_t t0 = 1791700000;  // epoch s
  int mix[3];
  l.add(PowerState::Active, t0 + 1800, 1800);
  TEST_ASSERT_FALSE(l.mix(t0 + 1800, mix));  // meno di un'ora: uso tipico
  l.add(PowerState::Idle, t0 + 1800 + 5400, 5400);
  l.add(PowerState::Sleep, t0 + 1800 + 5400 + 3600, 3600);
  TEST_ASSERT_TRUE(l.mix(t0 + 10800, mix));
  TEST_ASSERT_EQUAL(17, mix[0]);  // 1800 / 10800
  TEST_ASSERT_EQUAL(50, mix[1]);
  TEST_ASSERT_EQUAL(33, mix[2]);
  uint32_t tot[3];
  l.totals(t0 + 10800 + 30 * 3600, tot);  // più di 24 h dopo: tutto scaduto
  TEST_ASSERT_EQUAL_UINT32(0, tot[0] + tot[1] + tot[2]);
}

void test_sleep_rate_from_nights() {
  double r = 0;
  TEST_ASSERT_FALSE(sleepRateSample(4000, 3990, 3 * 3600, r));  // meno di 4 h
  TEST_ASSERT_TRUE(sleepRateSample(mvFromPercent(80), mvFromPercent(78), 8 * 3600, r));
  TEST_ASSERT_FLOAT_WITHIN(0.02, 0.25, r);
  TEST_ASSERT_FALSE(sleepRateSample(3900, 4100, 8 * 3600, r));  // la tensione è salita: cavo
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.3, mergeSleepRate(true, 0.4, 0.2));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.2, mergeSleepRate(false, 0.4, 0.2));
}

void test_bat_blob_roundtrip_and_checks() {
  BatInfo b;
  memset(&b, 0, sizeof b);
  b.cal.valid = true;
  b.cal.active = 8.04;
  b.cal.idle = 1.5;
  b.cal.atLocal = 1791714600000LL;
  b.hasSleep = true;
  b.sleep = 0.27;
  uint8_t blob[kBatBlobLen];
  TEST_ASSERT_EQUAL(kBatBlobLen, packBat(b, blob, sizeof blob));
  BatInfo c;
  memset(&c, 0, sizeof c);
  TEST_ASSERT_TRUE(unpackBat(blob, sizeof blob, c));
  TEST_ASSERT_TRUE(c.cal.valid);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 8.04, c.cal.active);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.5, c.cal.idle);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.27, c.sleep);
  TEST_ASSERT_EQUAL_INT64(b.cal.atLocal, c.cal.atLocal);
  blob[3] ^= 1;
  TEST_ASSERT_FALSE(unpackBat(blob, sizeof blob, c));
  TEST_ASSERT_FALSE(unpackBat(blob, sizeof blob - 1, c));
}

void test_snapshot_roundtrip() {
  static Model m;
  newModel(m, kT0, wcAt(kT0));
  m.sys.pcOff = true;
  m.sys.offAt = kT0 - 60000;
  m.sessions[1].hasAttnAt = true;
  m.sessions[1].attnAt = kT0 - 120000;
  static SleepSnap s;
  memset(&s, 0, sizeof s);
  const int64_t ep = 1791707400000LL;
  snapFromModel(s, m, ep, kT0);
  s.reason = uint8_t(SleepReason::Bye);
  snapSeal(s);
  TEST_ASSERT_TRUE(snapValid(s));
  static Model w;
  newModel(w, 5000, wcAt(5000));
  w.nSessions = 0;
  snapToModel(s, w, ep + 900000, 5000);  // 15 minuti dopo, su un orologio monotono ripartito
  TEST_ASSERT_EQUAL(4, w.nSessions);
  TEST_ASSERT_EQUAL_STRING("web-app", w.sessions[1].name);
  TEST_ASSERT_EQUAL(int(SessionState::Permission), int(w.sessions[1].state));
  TEST_ASSERT_EQUAL_INT64(5000 - 900000 - 120000, w.sessions[1].attnAt);
  TEST_ASSERT_EQUAL_STRING("Guida di installazione aggiornata e 4 link rotti corretti.", w.sessions[3].summary);
  TEST_ASSERT_TRUE(w.sys.pcOff);
  TEST_ASSERT_FALSE(w.sys.pc);
  TEST_ASSERT_EQUAL_INT64(5000 - 900000 - 60000, w.sys.offAt);
  s.n = 3;  // modificata dopo il sigillo
  TEST_ASSERT_FALSE(snapValid(s));
}

// ---------------------------------------------------------------- cavo USB: CP2102 alimentato o «fantasma»

// CP2102 finto. vbus = cavo collegato. Senza cavo, con GPIO43 alla UART (alta a riposo) i condensatori di VDD
// si caricano a 2,7 V attraverso R2 e i diodi; con GPIO43 basso e il pull-down su GPIO44 si scaricano con
// τ = 50 ms. GPIO44 legge alto se il CP2102 è alimentato (VBUS) o se VDD «fantasma» supera la soglia (1,65 V).
struct FakeCp2102 : UsbPins {
  bool vbus = false, phantom = true, inputOk = true;
  bool txUart = true;   // GPIO43 collegato alla UART
  double vdd = 2.7;
  uint32_t waited = 0;
  int begins = 0, txSwitches = 0;
  void begin(bool releaseTx) override { begins++; if (releaseTx) txUart = false; }
  void waitMs(uint32_t ms) override {
    waited += ms;
    if (!txUart) vdd *= exp(-double(ms) / 50.0);
  }
  bool sample(int &high, int &n) override {
    n = 8;
    const bool hi = vbus || (phantom && vdd > 1.65);
    high = hi ? 8 : 0;
    return inputOk;
  }
  void end(bool restoreTx) override { if (restoreTx) { txUart = true; charge(); } }
  void setTx(bool uart) override { txSwitches++; txUart = uart; if (uart) charge(); }
  void charge() { if (phantom && !vbus) vdd = 2.7; }
};

void test_usb_probe_cable_present() {
  FakeCp2102 p;
  p.vbus = true;
  const UsbProbe r = usbProbe(p, true);
  TEST_ASSERT_EQUAL(int(UsbPresent::Yes), int(r.present));
  TEST_ASSERT_EQUAL(8, r.high);
  TEST_ASSERT_TRUE(p.txUart);  // la TX torna alla UART
  TEST_ASSERT_EQUAL_UINT32(kUsbDrainMs, p.waited);
}

// Il bug dal vivo: senza attesa (lettura di prima) il CP2102 alimentato dalla TX dice «sì».
void test_usb_phantom_without_drain_reads_yes_with_drain_no() {
  FakeCp2102 p;
  int high = 0, n = 0;
  p.begin(false);
  p.sample(high, n);
  p.end(false);
  TEST_ASSERT_EQUAL(8, high);  // il falso «in carica»
  const UsbProbe r = usbProbe(p, true);
  TEST_ASSERT_EQUAL(int(UsbPresent::No), int(r.present));
  TEST_ASSERT_EQUAL(0, r.high);
}

void test_usb_unknown_when_input_off() {
  FakeCp2102 p;
  p.inputOk = false;
  TEST_ASSERT_EQUAL(int(UsbPresent::Unknown), int(usbProbe(p, true).present));
}

void test_usb_monitor_tx_off_while_unplugged_and_back() {
  FakeCp2102 p;
  UsbMonitor mon;
  TEST_ASSERT_FALSE(mon.check(p));  // primo «no»: lo stato stabile aspetta il secondo
  TEST_ASSERT_TRUE(mon.txOn());
  TEST_ASSERT_TRUE(mon.check(p));
  TEST_ASSERT_EQUAL(int(UsbPresent::No), int(mon.state()));
  TEST_ASSERT_FALSE(mon.txOn());   // niente log su UART0: la causa del fantasma è tolta
  TEST_ASSERT_FALSE(p.txUart);
  const uint32_t w = p.waited;
  for (int k = 0; k < 30; k++) TEST_ASSERT_FALSE(mon.check(p));  // 5 minuti a batteria: resta «no»
  TEST_ASSERT_EQUAL_UINT32(w, p.waited);  // con la TX già staccata nessuna attesa
  TEST_ASSERT_EQUAL(int(UsbPresent::No), int(mon.state()));
  p.vbus = true;  // cavo collegato
  TEST_ASSERT_TRUE(mon.check(p));
  TEST_ASSERT_EQUAL(int(UsbPresent::Yes), int(mon.state()));
  TEST_ASSERT_TRUE(mon.txOn());  // «[usb] cavo collegato · log ripreso»
  TEST_ASSERT_TRUE(p.txUart);
  TEST_ASSERT_EQUAL(8, mon.last().high);
}

// ---------------------------------------------------------------- calibrazione e autonomia (core/ui)

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(kT0) { reset(); }
  void reset() {
    now = kT0;
    ui.begin(now, wcAt(now));
    ui.takeBatChanged();
    advance(2700);  // avvio finito: Home
  }
  void advance(Ms ms) {
    const Ms end = now + ms;
    while (now < end) {
      now += 100;
      ui.tick(now, wcAt(now));
    }
  }
  Model &m() { return ui.model(); }
  void openBattery() {
    ui.act(Action::Menu, now, wcAt(now));
    for (int k = 0; k < 9; k++) ui.act(Action::Next, now, wcAt(now));
    ui.act(Action::Ok, now, wcAt(now));  // Menu › Batteria (sottomenu)
  }
  void startCalib() {
    openBattery();
    ui.act(Action::Next, now, wcAt(now));
    ui.act(Action::Ok, now, wcAt(now));  // Calibra la batteria
  }
};

void test_calibration_needs_90_percent() {
  static Bench b;
  b.reset();
  b.startCalib();  // batteria finta al 78%
  TEST_ASSERT_EQUAL(int(ScreenId::List), int(top(b.m()).id));
  TEST_ASSERT_EQUAL_STRING("Carica almeno al 90%", b.m().toast.msg);
  TEST_ASSERT_EQUAL(int(CalibPhase::Off), int(b.m().calib.phase));
}

void test_calibration_full_run_with_usb_pause() {
  static Bench b;
  b.reset();
  b.m().sys.batt = 100;
  b.startCalib();
  TEST_ASSERT_EQUAL(int(ScreenId::Calib), int(top(b.m()).id));
  TEST_ASSERT_EQUAL(int(CalibPhase::Max), int(b.m().calib.phase));
  TEST_ASSERT_EQUAL(1, b.m().calib.n);  // prima lettura subito
  TEST_ASSERT_TRUE(animRate(kHeltec213, b.m(), b.now) > 0);  // fase 1: animazione continua
  b.advance(10 * 60000);
  const Ms ms10 = b.m().calib.ms;
  b.m().sys.usb = true;  // cavo ricollegato: pausa
  b.advance(30 * 60000);
  TEST_ASSERT_TRUE(b.m().calib.paused);
  TEST_ASSERT_EQUAL_INT64(ms10, b.m().calib.ms);  // il tempo col cavo non conta
  TEST_ASSERT_EQUAL(0.0f, animRate(kHeltec213, b.m(), b.now));
  b.m().sys.usb = false;
  b.advance(51 * 60000);
  TEST_ASSERT_EQUAL(int(CalibPhase::Idle), int(b.m().calib.phase));
  TEST_ASSERT_TRUE(b.m().calib.hasMax);
  TEST_ASSERT_FLOAT_WITHIN(0.3, 8.0, b.m().calib.rMax);  // la scarica finta del simulatore: 8 %/h
  TEST_ASSERT_EQUAL(0.0f, animRate(kHeltec213, b.m(), b.now));  // fase 2 ferma
  TEST_ASSERT_FALSE(b.ui.takeBatChanged());
  b.advance(61 * 60000);
  TEST_ASSERT_EQUAL(int(CalibPhase::Done), int(b.m().calib.phase));
  TEST_ASSERT_TRUE(b.m().bat.cal.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.3, 1.5, b.m().bat.cal.idle);
  TEST_ASSERT_TRUE(b.ui.takeBatChanged());
  TEST_ASSERT_FALSE(b.ui.takeBatChanged());  // si legge una volta
}

void test_calibration_three_presses_abort() {
  static Bench b;
  b.reset();
  b.m().sys.batt = 95;
  b.startCalib();
  b.advance(60000);
  b.ui.act(Action::Next, b.now, wcAt(b.now));
  TEST_ASSERT_EQUAL_STRING("Calibrazione in corso: 3X per annullare", b.ui.lastAction());
  b.ui.act(Action::Sleep, b.now, wcAt(b.now));  // niente Riposo durante la prova
  TEST_ASSERT_EQUAL(int(ScreenId::Calib), int(top(b.m()).id));
  TEST_ASSERT_NULL(holdLabel(b.m(), b.now));
  b.ui.act(Action::Menu, b.now, wcAt(b.now));
  TEST_ASSERT_EQUAL(int(ScreenId::List), int(top(b.m()).id));
  TEST_ASSERT_EQUAL_STRING("Calibrazione annullata", b.m().toast.msg);
  TEST_ASSERT_EQUAL(int(CalibPhase::Off), int(b.m().calib.phase));
}

void test_calibration_with_cable_waits_then_checks_level() {
  static Bench b;
  b.reset();
  b.m().sys.usb = true;
  b.startCalib();
  TEST_ASSERT_EQUAL(int(CalibPhase::Wait), int(b.m().calib.phase));
  b.advance(5 * 60000);
  TEST_ASSERT_EQUAL(int(CalibPhase::Wait), int(b.m().calib.phase));
  b.m().sys.usb = false;  // staccato, ma la batteria è al 78%
  b.advance(200);
  TEST_ASSERT_EQUAL(int(CalibPhase::Off), int(b.m().calib.phase));
  TEST_ASSERT_EQUAL_STRING("Carica almeno al 90%", b.m().toast.msg);
}

void test_calibration_live_sample_comes_from_loop() {
  static Bench b;
  b.reset();
  b.m().sys.live = true;
  b.m().sys.batt = 99;
  b.startCalib();
  TEST_ASSERT_TRUE(b.ui.calibDue());
  TEST_ASSERT_EQUAL(0, b.m().calib.n);
  b.ui.calibSample(4150, b.now, wcAt(b.now));
  TEST_ASSERT_FALSE(b.ui.calibDue());
  TEST_ASSERT_EQUAL(1, b.m().calib.n);
  TEST_ASSERT_EQUAL(4150, b.m().calib.mv);
  b.advance(59000);
  TEST_ASSERT_FALSE(b.ui.calibDue());
  b.advance(1100);
  TEST_ASSERT_TRUE(b.ui.calibDue());
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 99, b.m().sys.batt);  // con la rete nessuna scarica finta
}

void test_new_battery_double_confirm() {
  static Bench b;
  b.reset();
  b.m().bat.cal.valid = true;
  b.m().bat.cal.active = 8;
  b.m().bat.cal.idle = 1.5;
  b.openBattery();
  b.ui.act(Action::Next, b.now, wcAt(b.now));
  b.ui.act(Action::Next, b.now, wcAt(b.now));  // Nuova batteria
  b.ui.act(Action::Ok, b.now, wcAt(b.now));
  TEST_ASSERT_TRUE(b.m().bat.cal.valid);
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", b.m().toast.msg);
  b.ui.act(Action::Ok, b.now, wcAt(b.now));
  TEST_ASSERT_FALSE(b.m().bat.cal.valid);
  TEST_ASSERT_EQUAL(1, top(b.m()).sel);  // propone «Calibra la batteria»
  TEST_ASSERT_TRUE(b.ui.takeBatChanged());
}

void test_regression_and_autonomy_texts() {
  double h[6], p[6], r = 0;
  for (int k = 0; k < 6; k++) { h[k] = k / 60.0; p[k] = 90 - 8.0 * h[k]; }
  TEST_ASSERT_FALSE(calRate(h, p, 4, r));
  TEST_ASSERT_TRUE(calRate(h, p, 6, r));
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 8.0, r);
  static Model m;
  newModel(m, kT0, wcAt(kT0));
  char t[64];
  autonomyText(m, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("Autonomia: stima in corso", t);
  m.sys.usb = true;
  autonomyText(m, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("Autonomia: calibra per una stima", t);
  m.sys.usb = false;
  m.bat.trend = 2;  // 78 % / 2 %/h = 39 h
  autonomyText(m, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("Autonomia: circa 39 ore (dalla scarica)", t);
  m.bat.cal.valid = true;
  m.bat.cal.active = 8;
  m.bat.cal.idle = 1.5;
  // 30 % × 8 + 50 % × 1,5 + 20 % × 0,3 (sonno non misurato) = 3,21 %/h → 78 / 3,21 = 24,3 h
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 3.21, battRate(m));
  autonomyText(m, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("Autonomia: circa 24 ore con il tuo uso", t);
  m.bat.mix[0] = 5;
  m.bat.mix[1] = 25;
  m.bat.mix[2] = 70;
  m.bat.hasSleep = true;
  m.bat.sleep = 0.25;  // 0,4 + 0,375 + 0,175 = 0,95 %/h → 82 h → 3 giorni
  autonomyText(m, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("Autonomia: circa 3 giorni con il tuo uso", t);
  voltText(4174, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("4,17 V", t);
  rateText(7.94, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("7,9 %/h", t);
  TEST_ASSERT_EQUAL(4190, mvFromPercent(100));
  TEST_ASSERT_EQUAL(3300, mvFromPercent(10));
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 10.0, ocvPercent(3300));
}

void test_bye_shows_summary_with_full_refresh() {
  static Bench b;
  b.reset();
  b.m().sys.live = true;
  b.m().nPerms = 1;
  const RefreshKind r = b.ui.netBye(b.now);
  TEST_ASSERT_EQUAL(int(RefreshKind::Full), int(r));
  TEST_ASSERT_TRUE(b.m().sys.pcOff);
  TEST_ASSERT_FALSE(b.m().sys.pc);
  TEST_ASSERT_EQUAL(0, b.m().nPerms);
  TEST_ASSERT_EQUAL(int(ScreenId::Home), int(top(b.m()).id));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(b.ui.netPcLost(b.now)));  // la chiusura 1001 non cambia niente
}

void test_idle_freezes_animations() {
  static Bench b;
  b.reset();
  TEST_ASSERT_TRUE(animRate(kHeltec213, b.m(), b.now) > 0);  // sessione in permesso: animazione
  b.m().sys.idle = true;
  TEST_ASSERT_EQUAL(0.0f, animRate(kHeltec213, b.m(), b.now));
}

void test_info_usb_value() {
  static Bench b;
  b.reset();
  static MenuList L;
  Model &m = b.m();
  m.sys.live = true;
  menuList(MenuId::Info, kHeltec213, m, b.now, wcAt(b.now), L);
  int k = 0;
  while (k < L.n && strcmp(L.items[k].label, "Cavo USB")) k++;
  TEST_ASSERT_TRUE(k < L.n);
  TEST_ASSERT_EQUAL_STRING("?", L.items[k].val);
  m.sys.usbHigh = 0;
  m.sys.usbN = 8;
  menuList(MenuId::Info, kHeltec213, m, b.now, wcAt(b.now), L);
  TEST_ASSERT_EQUAL_STRING("no (0/8)", L.items[k].val);
  m.sys.usbHigh = 8;
  menuList(MenuId::Info, kHeltec213, m, b.now, wcAt(b.now), L);
  TEST_ASSERT_EQUAL_STRING("sì (8/8)", L.items[k].val);
}

// Review Focus: con il Bluetooth acceso l'IDF vuole il modem sleep. 0 nessuno, 1 minimo, 2 massimo.
static void test_wifi_ps_never_none_with_ble() {
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(false, false, false));  // Attivo
  TEST_ASSERT_EQUAL_UINT8(2, wifiPowerSave(false, true, false));   // Attesa
  TEST_ASSERT_EQUAL_UINT8(0, wifiPowerSave(true, false, false));   // calibrazione, fase 1
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(true, false, true));    // …ma col Bluetooth: minimo
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(false, false, true));
  TEST_ASSERT_EQUAL_UINT8(2, wifiPowerSave(false, true, true));
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < 2; i++) TEST_ASSERT_NOT_EQUAL(0, wifiPowerSave(c, i, true));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_active_rules_and_idle_after_60s);
  RUN_TEST(test_usb_always_active_never_sleeps);
  RUN_TEST(test_rest_without_usb_sleeps);
  RUN_TEST(test_bye_grace_then_sleep);
  RUN_TEST(test_bye_then_service_restarts_no_sleep);
  RUN_TEST(test_bye_respects_recent_key);
  RUN_TEST(test_button_wake_after_bye_sleeps_again_after_60s);
  RUN_TEST(test_calibration_blocks_sleep_and_sets_state);
  RUN_TEST(test_battery_critical);
  RUN_TEST(test_power_inputs_from_model);
  RUN_TEST(test_timer_wake_decisions);
  RUN_TEST(test_retry_after_bye_every_15s);
  RUN_TEST(test_trend_needs_two_hours_and_resets_with_usb);
  RUN_TEST(test_ledger_mix_and_hours);
  RUN_TEST(test_sleep_rate_from_nights);
  RUN_TEST(test_bat_blob_roundtrip_and_checks);
  RUN_TEST(test_snapshot_roundtrip);
  RUN_TEST(test_usb_probe_cable_present);
  RUN_TEST(test_usb_phantom_without_drain_reads_yes_with_drain_no);
  RUN_TEST(test_usb_unknown_when_input_off);
  RUN_TEST(test_usb_monitor_tx_off_while_unplugged_and_back);
  RUN_TEST(test_calibration_needs_90_percent);
  RUN_TEST(test_calibration_full_run_with_usb_pause);
  RUN_TEST(test_calibration_three_presses_abort);
  RUN_TEST(test_calibration_with_cable_waits_then_checks_level);
  RUN_TEST(test_calibration_live_sample_comes_from_loop);
  RUN_TEST(test_new_battery_double_confirm);
  RUN_TEST(test_regression_and_autonomy_texts);
  RUN_TEST(test_bye_shows_summary_with_full_refresh);
  RUN_TEST(test_idle_freezes_animations);
  RUN_TEST(test_info_usb_value);
  RUN_TEST(test_wifi_ps_never_none_with_ble);
  return UNITY_END();
}
