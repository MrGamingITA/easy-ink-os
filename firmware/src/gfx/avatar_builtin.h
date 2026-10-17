// Avatar predefiniti compilati nel firmware (avatar_builtin.cpp, generato da tools/sim-capture/gen-avatars.mjs).
// L'ordine è quello dei pulsanti Simula del simulatore: goccia, gatto, demone, calamaro.
#pragma once
#include <stdint.h>

#include "avatar.h"

struct BuiltinAvatar {
  const char *id;
  const uint8_t *data;
  uint16_t len;
  uint32_t crc;  // anche nell'intestazione: qui per trovarlo senza leggere i byte
};
extern const BuiltinAvatar kBuiltinAvatars[];
extern const int kBuiltinAvatarCount;

// Vista validata dell'avatar predefinito i (0 = Goccia), letta la prima volta che serve. nullptr se il
// CAVT compilato non passa la validazione (non succede: lo controlla test_avatar).
const Avatar *builtinAvatar(int i);
// Per id ('' o sconosciuto → nullptr).
const Avatar *builtinAvatarById(const char *id);
// La Goccia: il ripiego di tutto.
const Avatar &gocciaAvatar();
