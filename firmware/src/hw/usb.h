// Cavo USB (Heltec Wireless Paper): livello di U0RXD (GPIO44) con un pull-down, dopo aver staccato
// U0TXD (GPIO43) dalla UART per scaricare il CP2102 alimentato «di fantasma». Logica in core/usb.h.
#pragma once
#include "core/usb.h"

namespace usb {

// I pin veri (GPIO43, GPIO44) per cal::UsbMonitor / cal::usbProbe.
cal::UsbPins &pins();

// Pad di GPIO44 trovato all'inizio dell'ultima lettura (registro IO_MUX), per la diagnosi nel log.
struct Pad { bool wpu, wpd, ie; };
Pad lastPad();

}  // namespace usb
