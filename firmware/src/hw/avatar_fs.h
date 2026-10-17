// Memoria degli avatar sulla scheda: cache dei CAVT ricevuti in LittleFS (partizione dati «spiffs» da
// 1,5 MB di default_8MB.csv, finora vuota) e ultimo avatar predefinito noto nella NVS.
// Perché LittleFS e non blob NVS: la NVS è di 20 KB (0x5000) e tiene già vault e impostazioni, mentre
// 8 avatar sono fino a 8 × 6 372 = 50 976 byte; in LittleFS si legge un fotogramma (175 byte) alla volta
// e la scrittura di un file nuovo seguita da una rinomina non lascia mezzi avatar dopo un'interruzione.
#pragma once
#include "core/accessories.h"
#include "core/avatars.h"

namespace avfs {

// File aperti insieme in LittleFS: avatar, accessori, salvataggi del Game Boy e temi (fino a 3 durante un download
// del tema: temporaneo in scrittura, sua rilettura, ordine). Stesso valore in ogni montaggio (vince il primo).
constexpr uint8_t kMaxOpenFiles = 6;

// Monta LittleFS (la prima volta formatta la partizione) e crea /av. false = solo i predefiniti.
bool begin();
cal::AvatarStorage &storage();
// Ultimo avatar predefinito arrivato dal servizio (welcome/state), per l'avvio prima della rete.
bool loadDefault(cal::AvatarRef &out);
void saveDefault(const cal::AvatarRef &r);
void forgetDefault();  // Ripristino di fabbrica

// 0.10.0, gioco della mascotte: accessori (/acc/<id>.cacc) e ancore (/anc/<id>.canc) ricevuti, un file per id, e
// l'ultimo riassunto del gioco (/av/pet). Stessa scrittura tutto o niente degli avatar.
cal::AccStorage &accStorage();
int loadPet(uint8_t *dst, int cap);  // byte letti, -1 se assente
bool savePet(const uint8_t *data, size_t n);
void forgetPet();  // Ripristino di fabbrica (accessori e ancore li toglie AccessoryStore::clear)

}  // namespace avfs
