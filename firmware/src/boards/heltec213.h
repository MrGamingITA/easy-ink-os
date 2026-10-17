// Heltec Wireless Paper V1.2 · pin verificati.
// Fonte: heltec-eink-modules 4.6.0, src/Platforms/WirelessPaper/WirelessPaper.h
// (classe EInkDisplay_WirelessPaperV1_2 = pannello E0213A367). Vedi docs/HARDWARE.md.
#pragma once

#define BOARD_NAME        "Heltec Wireless Paper"
#define BOARD_W           250
#define BOARD_H           122
#define BOARD_CLASS       'S'

// Profilo pannello (stime del simulatore, DEVICES.heltec213): da aggiornare con le misure.
#define BOARD_FPS         2
#define BOARD_FULL_EVERY  40

#define PIN_BUTTON_PRG    0     // tasto PRG, attivo basso; è anche il pin di boot
#define PIN_LED           18
#define PIN_VEXT          45    // alimentazione periferiche (pannello), attiva BASSA

// Pannello SPI (gestiti dalla libreria): MOSI 2, CLK 3, CS 4, DC 5, RST 6, BUSY 7.
// Batteria (schema ufficiale Wireless_Paper_V0.4, uguale per V1.2 secondo il registro hardware Heltec;
// concordano Meshtastic variants/esp32s3/heltec_wireless_paper/variant.h). Vedi docs/HARDWARE.md.
#define PIN_VBAT_ADC      20    // rete ADC_IN = VBAT × R26 / (R21 + R26), ADC2 canale 9
#define PIN_VBAT_CTRL     19    // rete ADC_Ctrl: gate del P-MOSFET Q3 (AO3401), partitore acceso con BASSO
#define VBAT_CTRL_ON      LOW
// Stato di carica verso l'MCU: NON esiste (CHRG del TP4054 va solo al LED arancione). Cavo USB: si ricava
// dalla TXD del CP2102 (alimentato solo dalla USB) su U0RXD = GPIO44, vedi hw/usb.cpp.
#define PIN_USB_SENSE     44
// U0TXD: va alla RXD del CP2102 attraverso R2 (499 Ω); senza cavo può alimentarlo «di fantasma» (core/usb.h, M6)
#define PIN_UART_TX       43
