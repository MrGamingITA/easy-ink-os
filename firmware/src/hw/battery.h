// Lettura vera della batteria sulla Heltec Wireless Paper (solo sulla scheda; la curva è in core/battery).
#pragma once

namespace battery {

// Una volta, prima della prima lettura: attenuazione e caratterizzazione dell'ADC2, partitore spento.
void begin();

// Accende il partitore, aspetta 10 ms, media di 16 campioni calibrati (esp_adc_cal), spegne il partitore.
// Restituisce la tensione della batteria in mV, oppure −1 se la lettura non è buona (ADC2 occupato dal
// Wi-Fi in più di metà dei campioni, batteria assente). Mai durante un refresh: la chiama il loop,
// e display.update() è bloccante.
int readMv(int samples = 16);  // M6: la calibrazione ne usa 64 (media di molti campioni)

// Prima del sonno profondo: partitore spento e GPIO19 tenuto ALTO anche durante il sonno (gpio_hold).
void prepareSleep();
// Al risveglio: toglie il blocco su GPIO19 (prima di begin()).
void releaseHold();

}  // namespace battery
