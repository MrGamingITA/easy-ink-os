// Giochi Game Boy sulla scheda: partizione «gbrom» (partitions_gb.csv), scritta col cavo (esptool), letta
// dall'intestazione e mappata in memoria per l'emulatore (mai copiata in RAM: niente PSRAM); salvataggi della
// cartuccia in LittleFS (/gb/<id>.sav, montato da avfs::begin), scritti tutto o niente.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/gb_rom.h"

namespace gbhw {

// Cerca la partizione, crea /gb e legge l'intestazione. false = tabella delle partizioni vecchia: niente giochi.
bool begin();
bool partitionOk();
bool savesOk();  // LittleFS montato: senza, i salvataggi non si possono scrivere
uint32_t capacity();
// Intestazione valida della ROM nella partizione (letta da begin): false = partizione vuota o rovinata.
bool romInfo(cal::gb::RomInfo &out);
// ROM mappata in memoria (MMU della cache, sola lettura). nullptr = mappatura non riuscita.
const uint8_t *mapRom(uint32_t size);
void unmapRom();
// RAM della cartuccia in banchi da cal::gb::kRamBank byte (l'ultimo può essere più corto: size totale). Il file è la
// RAM più 4 byte di CRC-32 (core/gb_save). Si scrive /gb/<id>.tmp, si rilegge e si controlla, poi si rinomina sopra
// il .sav (LittleFS sostituisce in un colpo solo): il salvataggio buono non si cancella mai prima.
enum class SaveRead : uint8_t { Missing, Ok, Bad, Stuck };
// Bad: il .sav c'è ma è rovinato (dimensione o CRC): rinominato in /gb/<id>.bad*, i banchi non valgono.
// Stuck: rovinato e non spostato (copie .bad già piene o rinomina non riuscita): resta dov'è.
SaveRead readSave(const char *id, uint8_t *const *banks, size_t size);
bool writeSave(const char *id, const uint8_t *const *banks, size_t size);

}  // namespace gbhw
