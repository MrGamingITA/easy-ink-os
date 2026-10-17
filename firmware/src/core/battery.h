// Batteria LiPo 1S: dalla tensione letta sul partitore alla percentuale (logica pura, provata in native).
// La lettura vera dell'ADC sta in hw/battery.cpp (solo sulla scheda). Fonti in docs/HARDWARE.md.
#pragma once
#include <stdint.h>

namespace cal {

// Rapporto del partitore della Heltec Wireless Paper: R21 10 kΩ (VBAT) e R26 10 kΩ (GND), resistenze al
// 5 %: VBAT = 2 × ADC_IN (schema ufficiale V0.4, rete ADC_IN su GPIO20, abilitazione ADC_Ctrl su GPIO19).
constexpr int kBattDividerNum = 2;

// Letture fuori da questo intervallo non sono una LiPo collegata (partitore spento, batteria assente,
// ADC2 occupato): si scartano e resta l'ultimo valore buono.
constexpr int kBattMinMv = 2600;  // 3100 (0 %) − 500, come noBatVolt di Meshtastic
constexpr int kBattMaxMv = 4500;  // sopra 4,2 V + 1 % del TP4054 con margine

// Tensione a vuoto → percentuale, interpolazione lineare sulla tabella OCV a passi del 10 %
// (4190 mV = 100 %, 3100 mV = 0 %). Fuori tabella: 0 o 100.
int lipoPercent(int mv);

// Media delle letture valide: n campioni grezzi in mV al pin, ok[i] = falso se la lettura è fallita.
// Restituisce la tensione della batteria in mV (× kBattDividerNum), oppure −1 se meno di metà dei
// campioni è valida o se il risultato è fuori da [kBattMinMv, kBattMaxMv].
int batteryMv(const int *pinMv, const bool *ok, int n);

}  // namespace cal
