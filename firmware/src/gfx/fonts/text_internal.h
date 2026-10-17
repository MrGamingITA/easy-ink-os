// Funzioni di text.cpp esposte solo ai test (non fanno parte dell'interfaccia di text.h).
#pragma once
#include <stdint.h>

// Larghezza come c.measureText(s).width, in millesimi di pixel (senza arrotondare).
int32_t textWidthMilli(const char *s, int size, int weight = 600, bool pix = false);
