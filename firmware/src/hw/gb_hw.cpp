#include "hw/gb_hw.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <stdio.h>
#include <string.h>

#include "core/gb_emu.h"
#include "core/gb_save.h"
#include "hw/logtee.h"

namespace gbhw {

namespace {

const char *const kDir = "/gb";
const esp_partition_t *gPart = nullptr;
spi_flash_mmap_handle_t gMap = 0;
bool gMapped = false, gFs = false, gRomOk = false;
cal::gb::RomInfo gInfo;

// /gb/<id>.sav e la sua copia in scrittura /gb/<id>.tmp: si scrive la copia, poi via il vecchio e rinomina. Un
// riavvio fra le due lascia solo la copia, completa: readSave la riconosce dalla dimensione e la rinomina.
void path(const char *id, const char *ext, char *out, size_t cap) { snprintf(out, cap, "/gb/%s.%s", id, ext); }

size_t bankLen(size_t size, int i) {
  const size_t from = size_t(i) * cal::gb::kRamBank;
  return size - from < cal::gb::kRamBank ? size - from : cal::gb::kRamBank;
}

}  // namespace

bool begin() {
  gPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "gbrom");
  if (!gPart) {
    LogSerial.println("[gb] partizione gbrom assente: tabella vecchia, giochi spenti (serve il flash via cavo)");
    return false;
  }
  gFs = LittleFS.totalBytes() > 0;  // montato da avfs::begin
  if (gFs) {
    if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  } else {
    LogSerial.println("[gb] LittleFS non montato: la partita non si potrà salvare");
  }
  uint8_t h[cal::gb::kHeaderLen];
  gRomOk = esp_partition_read(gPart, 0, h, sizeof h) == ESP_OK && cal::gb::parseHeader(h, sizeof h, gInfo) &&
           cal::gb::fitsPartition(gInfo, gPart->size);
  if (gRomOk)
    LogSerial.printf("[gb] partizione gbrom 0x%06lx, %lu KB: «%s», %lu KB, cartuccia 0x%02x, RAM %lu byte\n",
                     (unsigned long)gPart->address, (unsigned long)(gPart->size / 1024), gInfo.title,
                     (unsigned long)(gInfo.size / 1024), gInfo.cartType, (unsigned long)gInfo.ramBytes);
  else
    LogSerial.printf("[gb] partizione gbrom 0x%06lx, %lu KB: nessuna ROM valida\n", (unsigned long)gPart->address,
                     (unsigned long)(gPart->size / 1024));
  return true;
}

bool partitionOk() { return gPart != nullptr; }
bool savesOk() { return gFs; }
uint32_t capacity() { return gPart ? gPart->size : 0; }

bool romInfo(cal::gb::RomInfo &out) {
  if (!gRomOk) return false;
  out = gInfo;
  return true;
}

const uint8_t *mapRom(uint32_t size) {
  if (!gPart || gMapped || size > gPart->size) return nullptr;
  const uint32_t len = (size + SPI_FLASH_MMU_PAGE_SIZE - 1) / SPI_FLASH_MMU_PAGE_SIZE * SPI_FLASH_MMU_PAGE_SIZE;
  const void *p = nullptr;
  const esp_err_t e = esp_partition_mmap(gPart, 0, len, SPI_FLASH_MMAP_DATA, &p, &gMap);
  if (e != ESP_OK) {
    LogSerial.printf("[gb] mappatura della ROM non riuscita: %s\n", esp_err_to_name(e));
    return nullptr;
  }
  gMapped = true;
  return static_cast<const uint8_t *>(p);
}

void unmapRom() {
  if (gMapped) spi_flash_munmap(gMap);
  gMapped = false;
}

namespace {

// Legge il file nei banchi (o solo per controllo, banks = nullptr) e confronta il CRC con la coda.
bool loadChecked(const char *p, uint8_t *const *banks, size_t size) {
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  bool ok = f.size() == cal::gb::fileLen(size);
  uint32_t crc = cal::gb::kCrcInit;
  uint8_t buf[256];
  for (size_t off = 0; ok && off < size;) {
    const int i = int(off / cal::gb::kRamBank);
    size_t n = bankLen(size, i) - off % cal::gb::kRamBank;
    if (!banks && n > sizeof buf) n = sizeof buf;
    uint8_t *dst = banks ? banks[i] + off % cal::gb::kRamBank : buf;
    ok = f.read(dst, n) == n;
    crc = cal::gb::crc32Update(crc, dst, n);
    off += n;
  }
  uint8_t t[cal::gb::kTrailerLen];
  ok = ok && f.read(t, sizeof t) == sizeof t && cal::gb::unpackTrailer(t) == cal::gb::crc32Finish(crc);
  f.close();
  return ok;
}

// Il .sav rovinato diventa .bad, .bad1 o .bad2 (mai si cancella una copia messa da parte). Tutte e tre occupate:
// resta dov'è e false (la partita non parte finché qualcuno non guarda).
bool putAside(const char *id, const char *p) {
  static const char *const kExt[] = {"bad", "bad1", "bad2"};
  char b[32];
  for (int i = 0; i < 3; i++) {
    path(id, kExt[i], b, sizeof b);
    if (LittleFS.exists(b)) continue;
    const bool ok = LittleFS.rename(p, b);
    LogSerial.printf("[gb] salvataggio %s rovinato (dimensione o CRC): %s %s\n", id, ok ? "messo da parte in" : "NON spostato in", b);
    return ok;
  }
  LogSerial.printf("[gb] salvataggio %s rovinato e già 3 copie messe da parte: lasciato com'è\n", id);
  return false;
}

bool replace(const char *from, const char *to) {
  if (LittleFS.rename(from, to)) return true;  // LittleFS sostituisce la destinazione in un colpo solo
  if (LittleFS.exists(to) && LittleFS.remove(to)) return LittleFS.rename(from, to);  // ultima risorsa
  return false;
}

}  // namespace

SaveRead readSave(const char *id, uint8_t *const *banks, size_t size) {
  if (!gFs || !size) return SaveRead::Missing;
  char p[32], t[32];
  path(id, "sav", p, sizeof p);
  path(id, "tmp", t, sizeof t);
  if (LittleFS.exists(p)) {
    if (loadChecked(p, banks, size)) {
      if (LittleFS.exists(t)) LittleFS.remove(t);  // scrittura interrotta: vale il salvataggio di prima
      return SaveRead::Ok;
    }
    // .sav rovinato: prima si guarda la copia in scrittura, se c'è; buona = è il salvataggio più recente.
    const bool tmpOk = LittleFS.exists(t) && loadChecked(t, banks, size);
    if (!putAside(id, p)) return SaveRead::Stuck;
    if (tmpOk) {
      if (replace(t, p)) {
        LogSerial.printf("[gb] salvataggio %s ripreso dalla copia in scrittura\n", id);
        return SaveRead::Ok;
      }
      LogSerial.printf("[gb] salvataggio %s: copia in scrittura buona ma non rinominata, lasciata in /gb\n", id);
      return SaveRead::Bad;  // la copia verificata resta: al prossimo avvio si riprende
    }
    if (LittleFS.exists(t)) LittleFS.remove(t);  // solo una copia non valida
    return SaveRead::Bad;
  }
  if (!LittleFS.exists(t)) return SaveRead::Missing;
  // Solo la copia in scrittura: buona se completa e col CRC giusto (riavvio fra scrittura e rinomina).
  const bool whole = loadChecked(t, banks, size);
  LogSerial.printf("[gb] salvataggio %s: copia in scrittura %s\n", id, whole ? "completa, ripresa" : "incompleta, scartata");
  if (!whole) {
    LittleFS.remove(t);
    return SaveRead::Missing;
  }
  replace(t, p);
  return SaveRead::Ok;
}

bool writeSave(const char *id, const uint8_t *const *banks, size_t size) {
  if (!gFs || !size) return false;
  char p[32], t[32];
  path(id, "sav", p, sizeof p);
  path(id, "tmp", t, sizeof t);
  File f = LittleFS.open(t, "w");
  if (!f) return false;
  bool ok = true;
  uint32_t crc = cal::gb::kCrcInit;
  for (int i = 0; ok && size_t(i) * cal::gb::kRamBank < size; i++) {
    const size_t n = bankLen(size, i);
    ok = f.write(banks[i], n) == n;
    crc = cal::gb::crc32Update(crc, banks[i], n);
  }
  uint8_t tr[cal::gb::kTrailerLen];
  cal::gb::packTrailer(cal::gb::crc32Finish(crc), tr);
  ok = ok && f.write(tr, sizeof tr) == sizeof tr;
  f.close();
  ok = ok && loadChecked(t, nullptr, size);  // riletto dalla flash: dimensione e CRC
  if (!ok || !replace(t, p)) {
    LittleFS.remove(t);
    return false;  // il .sav di prima resta com'era
  }
  return true;
}

}  // namespace gbhw
