// GPIO20 è sull'ADC2: con il Wi-Fi acceso adc2_get_raw() può fallire (ESP_ERR_TIMEOUT, IDF 4.4 «ADC
// Limitations»). analogReadMilliVolts() in quel caso converte uno 0 e restituisce un valore plausibile
// ma falso, quindi qui si usa l'API dell'IDF e si scartano i campioni falliti.
#include "hw/battery.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_adc_cal.h>

#include "boards/heltec213.h"
#include "core/battery.h"
#include "hw/logtee.h"

namespace battery {

namespace {
constexpr int kMaxSamples = 64;
constexpr adc2_channel_t kChannel = ADC2_CHANNEL_9;  // GPIO20 (soc/adc_channel.h: ADC2_GPIO20_CHANNEL)
// 12 dB: 0–3100 mV al pin (IDF 4.4); la batteria piena dà circa 2100 mV dopo il partitore.
constexpr adc_atten_t kAtten = ADC_ATTEN_DB_12;
esp_adc_cal_characteristics_t chars;
bool ready = false;
}  // namespace

void begin() {
  pinMode(PIN_VBAT_CTRL, OUTPUT);
  digitalWrite(PIN_VBAT_CTRL, !VBAT_CTRL_ON);
  adc2_config_channel_atten(kChannel, kAtten);
  const esp_adc_cal_value_t how = esp_adc_cal_characterize(ADC_UNIT_2, kAtten, ADC_WIDTH_BIT_12, 1100, &chars);
  LogSerial.printf("[batteria] ADC2 canale 9 (GPIO20), 12 dB, calibrazione %s\n",
                how == ESP_ADC_CAL_VAL_EFUSE_TP ? "eFuse a due punti" : how == ESP_ADC_CAL_VAL_EFUSE_VREF ? "eFuse Vref" : "predefinita");
  ready = true;
}

int readMv(int samples) {
  if (!ready) begin();
  if (samples < 1) samples = 1;
  if (samples > kMaxSamples) samples = kMaxSamples;
  digitalWrite(PIN_VBAT_CTRL, VBAT_CTRL_ON);
  delay(10);  // il partitore si assesta (stesso tempo di battery_adcEnable() di Meshtastic)
  int mv[kMaxSamples];
  bool ok[kMaxSamples];
  for (int i = 0; i < samples; i++) {
    int raw = 0;
    ok[i] = adc2_get_raw(kChannel, ADC_WIDTH_BIT_12, &raw) == ESP_OK;
    mv[i] = ok[i] ? int(esp_adc_cal_raw_to_voltage(uint32_t(raw), &chars)) : 0;
  }
  digitalWrite(PIN_VBAT_CTRL, !VBAT_CTRL_ON);
  int good = 0;
  for (int i = 0; i < samples; i++) good += ok[i];
  const int v = cal::batteryMv(mv, ok, samples);
  LogSerial.printf("[batteria] %d mV (%d/%d campioni validi) -> %s\n", v, good, samples, v < 0 ? "scartata" : "ok");
  return v;
}

void prepareSleep() {
  pinMode(PIN_VBAT_CTRL, OUTPUT);
  digitalWrite(PIN_VBAT_CTRL, !VBAT_CTRL_ON);
  gpio_hold_en(static_cast<gpio_num_t>(PIN_VBAT_CTRL));  // GPIO19 è un pin RTC: il livello resta nel sonno
}

void releaseHold() { gpio_hold_dis(static_cast<gpio_num_t>(PIN_VBAT_CTRL)); }

}  // namespace battery
